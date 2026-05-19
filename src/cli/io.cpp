#include "io.hpp"

#include <libslic3r/Format/bbs_3mf.hpp>
#include <libslic3r/Semver.hpp>
#include <libslic3r/Config.hpp>

#include <boost/filesystem.hpp>
#include <stdexcept>

namespace orca_cli {

namespace fs = boost::filesystem;

ProjectState load_project(const std::string& path)
{
    using namespace Slic3r;

    ProjectState s;
    s.model          = std::make_unique<Model>();
    s.project_config = std::make_unique<DynamicPrintConfig>();

    PlateDataPtrs plate_data;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    bool   is_bbl_3mf  = false;
    bool   is_orca_3mf = false;
    Semver file_version;

    const bool ok = load_bbs_3mf(
        path.c_str(),
        s.project_config.get(),
        &substitutions,
        s.model.get(),
        &plate_data,
        /*project_presets*/ nullptr,
        &is_bbl_3mf,
        &is_orca_3mf,
        &file_version,
        /*proFn*/ nullptr,
        LoadStrategy::LoadModel | LoadStrategy::LoadConfig);

    if (!ok) {
        // load_bbs_3mf may have populated plate_data partially; release any
        // entries it owns so we don't leak.
        release_PlateData_list(plate_data);
        throw std::runtime_error("load_bbs_3mf failed: " + path);
    }

    // Take ownership of every PlateData* that the loader allocated.
    for (PlateData* pd : plate_data)
        s.plates.emplace_back(pd);
    // Clear plate_data so release_PlateData_list (if ever called again) does
    // not double-free; we now own the pointers via unique_ptr in s.plates.
    plate_data.clear();

    // Rebuild PlateData::objects_and_instances from the obj_inst_map <-> loaded_id
    // pairing. The loader fills in obj_inst_map (plate-side identify_id mapping)
    // but does not always rebuild objects_and_instances in the form the saver
    // expects, so re-derive it here from ModelInstance::loaded_id.
    for (auto& plate : s.plates) {
        plate->objects_and_instances.clear();
        for (const auto& kv : plate->obj_inst_map) {
            const int identify_id = kv.first;
            for (size_t oi = 0; oi < s.model->objects.size(); ++oi) {
                const ModelObject* obj = s.model->objects[oi];
                for (size_t ii = 0; ii < obj->instances.size(); ++ii) {
                    if (obj->instances[ii]->loaded_id == identify_id) {
                        plate->objects_and_instances.emplace_back(int(oi), int(ii));
                    }
                }
            }
        }
    }

    return s;
}

void save_project(const ProjectState& s, const std::string& target_path)
{
    using namespace Slic3r;

    // Write to "<target>.tmp" then atomic-rename, so a crash mid-write does
    // not leave a half-written .3mf at the destination path.
    fs::path tmp = fs::path(target_path).string() + ".tmp";
    // store_bbs_3mf takes a `const char*` for the path. Bind the string to a
    // local so it outlives the StoreParams.
    std::string tmp_path = tmp.string();

    PlateDataPtrs plate_data = s.plate_data_ptrs();

    StoreParams sp;
    sp.path             = tmp_path.c_str();
    sp.model            = s.model.get();
    sp.plate_data_list  = plate_data;
    sp.config           = s.project_config.get();
    // Default in the struct already includes Zip64, which is what the GUI uses
    // when saving a regular project; do not change it without strong reason.
    sp.strategy         = SaveStrategy::Zip64;

    const bool ok = store_bbs_3mf(sp);
    if (!ok) {
        boost::system::error_code ec;
        fs::remove(tmp, ec);
        throw std::runtime_error("store_bbs_3mf failed: " + target_path);
    }

    // Atomic-ish rename. boost::filesystem::rename on Windows overwrites the
    // destination if it exists (MoveFileEx with MOVEFILE_REPLACE_EXISTING via
    // boost), so remove any pre-existing file first to keep behaviour
    // predictable across platforms.
    boost::system::error_code ec;
    fs::remove(target_path, ec);
    fs::rename(tmp, target_path);
}

} // namespace orca_cli
