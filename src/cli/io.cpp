#include "io.hpp"
#include "invariants.hpp"
#include "png_placeholder.hpp"

#include <libslic3r/Format/bbs_3mf.hpp>
#include <libslic3r/Semver.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/miniz_extension.hpp>

#include <boost/filesystem.hpp>
#include <regex>
#include <stdexcept>
#include <unordered_set>

namespace orca_cli {

namespace fs = boost::filesystem;

namespace {

// store_bbs_3mf always writes relationship entries pointing at
//   /Metadata/plate_<n>.png            (cover-thumbnail-middle)
//   /Metadata/plate_<n>_small.png      (cover-thumbnail-small)
// regardless of whether those files actually get written into the archive.
// When the CLI is just cloning a known-good source 3mf (the `project init`
// case) we have those exact PNG blobs sitting in the source archive already
// -- carry them through verbatim so the resulting archive's relationships
// file does not dangle.
//
// We additionally copy through plate_no_light_<n>.png, top_<n>.png, and
// pick_<n>.png when they exist in the source: store_bbs_3mf does NOT add
// relationship entries for those files, but the Orca GUI loads them when
// present and falls back gracefully when absent, so they are safe to copy
// through and improve fidelity for the clone-and-mutate flow.
//
// When a plate exists in the project that does NOT have a corresponding
// PNG entry in either the target or the source (i.e. a plate created by
// `plate add` -- there is no source PNG to copy through), we inject a
// 128x128 gray placeholder PNG (orca_cli::make_placeholder_png_128_gray_C0)
// for both Metadata/plate_<n>.png and Metadata/plate_<n>_small.png. This
// keeps the runtime invariant guard (verify_plate_thumbnails + the
// dangling-relationships check) happy without forcing the CLI to render.
//
// This function falls through (no rewrite) when:
//   * the target archive cannot be opened (best-effort);
//   * there is nothing to copy from source AND no placeholder injections
//     are required.
//
// The output archive is rewritten in place: we open it as a reader, build a
// list of entries to keep + entries to copy from source + entries to
// synthesize, then write a new archive at the same path. miniz lacks a
// robust in-place append API for arbitrary zip files
// (`mz_zip_writer_init_from_reader` requires the source to be writable
// and has alignment constraints we cannot rely on).
void passthrough_missing_thumbnails(const ProjectState& s,
                                    const std::string&  target_zip_path)
{
    using namespace Slic3r;
    namespace fs = boost::filesystem;

    const std::string& source_zip_path = s.source_path;
    const bool has_source =
        !source_zip_path.empty() && fs::exists(source_zip_path);

    // Pattern for files that store_bbs_3mf references via .rels but does not
    // emit unless thumbnail_data is provided in StoreParams. Plate-numbered
    // PNG blobs only.
    static const std::regex thumbnail_re(
        R"(^Metadata/(plate_\d+(_small)?|plate_no_light_\d+|top_\d+|pick_\d+)\.png$)");

    // Open target as reader to enumerate existing entries.
    mz_zip_archive tgt_reader{};
    if (!open_zip_reader(&tgt_reader, target_zip_path))
        return;

    std::unordered_set<std::string> target_entries;
    const mz_uint tgt_count = mz_zip_reader_get_num_files(&tgt_reader);
    target_entries.reserve(tgt_count);
    for (mz_uint i = 0; i < tgt_count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&tgt_reader, i, &st)) continue;
        if (st.m_is_directory)                             continue;
        std::string name(st.m_filename);
        std::replace(name.begin(), name.end(), '\\', '/');
        target_entries.insert(name);
    }

    // Plan source-copy: collect (source_index, name) pairs for thumbnail
    // blobs present in source but missing in target. Best-effort -- if the
    // source archive cannot be opened, we just skip the copy phase and
    // proceed to placeholder injection (which is what `plate add` on an
    // in-memory ProjectState needs anyway).
    struct PendingCopy { mz_uint src_index; std::string name; };
    std::vector<PendingCopy> to_copy;
    mz_zip_archive src_reader{};
    bool src_opened = false;
    if (has_source && open_zip_reader(&src_reader, source_zip_path)) {
        src_opened = true;
        const mz_uint src_count = mz_zip_reader_get_num_files(&src_reader);
        for (mz_uint i = 0; i < src_count; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&src_reader, i, &st)) continue;
            if (st.m_is_directory)                             continue;
            std::string name(st.m_filename);
            std::replace(name.begin(), name.end(), '\\', '/');
            if (!std::regex_match(name, thumbnail_re)) continue;
            if (target_entries.find(name) != target_entries.end()) continue;
            to_copy.push_back({i, std::move(name)});
        }
    }

    // Plan placeholder injections: for every plate index 1..plates.size(),
    // if neither the target nor the source-copy plan covers
    // Metadata/plate_<n>.png and Metadata/plate_<n>_small.png, synthesize a
    // 128x128 gray placeholder. Plate numbering is 1-based on disk (see
    // verify_plate_thumbnails / store_bbs_3mf).
    std::unordered_set<std::string> planned_copy_names;
    planned_copy_names.reserve(to_copy.size());
    for (const auto& c : to_copy) planned_copy_names.insert(c.name);

    std::vector<std::string> to_synthesize;
    for (size_t i = 1; i <= s.plates.size(); ++i) {
        const std::string mid  = "Metadata/plate_" + std::to_string(i) + ".png";
        const std::string small = "Metadata/plate_" + std::to_string(i) + "_small.png";
        for (const std::string& name : {mid, small}) {
            if (target_entries.count(name))     continue;
            if (planned_copy_names.count(name)) continue;
            to_synthesize.push_back(name);
        }
    }

    if (to_copy.empty() && to_synthesize.empty()) {
        if (src_opened) close_zip_reader(&src_reader);
        close_zip_reader(&tgt_reader);
        return;
    }

    // Rewrite the target archive: copy every existing entry, then append the
    // missing thumbnail blobs from source, then append synthesized
    // placeholders. We close the target reader after we are done reading;
    // for the rewrite phase we read into memory then re-emit. This is fine
    // for project files in the MB range.
    std::vector<std::pair<std::string, std::vector<unsigned char>>> tgt_blobs;
    tgt_blobs.reserve(tgt_count);
    for (mz_uint i = 0; i < tgt_count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&tgt_reader, i, &st)) continue;
        if (st.m_is_directory)                             continue;
        std::vector<unsigned char> bytes(static_cast<size_t>(st.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&tgt_reader, i, bytes.data(), bytes.size(), 0))
            continue;
        std::string name(st.m_filename);
        std::replace(name.begin(), name.end(), '\\', '/');
        tgt_blobs.emplace_back(std::move(name), std::move(bytes));
    }

    std::vector<std::pair<std::string, std::vector<unsigned char>>> src_blobs;
    src_blobs.reserve(to_copy.size());
    if (src_opened) {
        for (const auto& c : to_copy) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&src_reader, c.src_index, &st)) continue;
            std::vector<unsigned char> bytes(static_cast<size_t>(st.m_uncomp_size));
            if (!mz_zip_reader_extract_to_mem(&src_reader, c.src_index, bytes.data(), bytes.size(), 0))
                continue;
            src_blobs.emplace_back(c.name, std::move(bytes));
        }
    }

    if (src_opened) close_zip_reader(&src_reader);
    close_zip_reader(&tgt_reader);

    // Build the placeholder PNG bytes once and reuse for every synthesized
    // entry. Both plate_N.png and plate_N_small.png get the same 128x128
    // gray placeholder; the GUI may render the same image at two sizes but
    // will not fail to load or crash. This is acceptable for v1.
    std::vector<char> placeholder;
    if (!to_synthesize.empty())
        placeholder = make_placeholder_png_128_gray_C0();

    // Rewrite the archive. Use the libslic3r helper so we honor the same
    // UTF-8 path conventions store_bbs_3mf used.
    const std::string rewrite_path = target_zip_path + ".rewrite";
    mz_zip_archive writer{};
    if (!open_zip_writer(&writer, rewrite_path))
        return;
    for (const auto& kv : tgt_blobs) {
        // PNG and config blobs are already compressed / small; use default
        // compression to match what store_bbs_3mf does.
        mz_zip_writer_add_mem(&writer, kv.first.c_str(),
                              kv.second.data(), kv.second.size(),
                              MZ_DEFAULT_COMPRESSION);
    }
    for (const auto& kv : src_blobs) {
        mz_zip_writer_add_mem(&writer, kv.first.c_str(),
                              kv.second.data(), kv.second.size(),
                              MZ_NO_COMPRESSION); // PNG is already compressed
    }
    for (const std::string& name : to_synthesize) {
        mz_zip_writer_add_mem(&writer, name.c_str(),
                              placeholder.data(), placeholder.size(),
                              MZ_NO_COMPRESSION); // PNG is already compressed
    }
    mz_zip_writer_finalize_archive(&writer);
    close_zip_writer(&writer);

    // Atomic rename-swap: rename target -> .bak, rename rewrite -> target,
    // remove .bak. If anything fails midway, restore the original via the .bak
    // so we never end up with the target file missing on disk. The previous
    // remove+rename sequence could lose the .tmp file on a rare
    // rename-failure path and produce a misleading "cannot open archive"
    // error downstream from run_all_invariants.
    const fs::path bak = fs::path(target_zip_path + ".bak");
    boost::system::error_code ec;
    fs::rename(target_zip_path, bak, ec);
    if (ec) {
        // Couldn't even start the swap -- abort the passthrough cleanly.
        boost::system::error_code ec2;
        fs::remove(rewrite_path, ec2);
        throw InvariantViolation(
            "thumbnail passthrough rewrite-swap failed (could not rename target): " + ec.message());
    }
    fs::rename(rewrite_path, target_zip_path, ec);
    if (ec) {
        // Restore the original so we don't lose data.
        boost::system::error_code ec2;
        fs::rename(bak, target_zip_path, ec2);
        fs::remove(rewrite_path, ec2);
        throw InvariantViolation(
            "thumbnail passthrough rewrite-swap failed (could not install rewrite): " + ec.message());
    }
    fs::remove(bak, ec);
}

} // namespace

ProjectState load_project(const std::string& path)
{
    using namespace Slic3r;

    ProjectState s;
    s.model          = std::make_unique<Model>();
    s.project_config = std::make_unique<DynamicPrintConfig>();
    s.source_path    = path;

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

    // Carry through plate thumbnail blobs from the originating archive (if
    // any) before running the invariant guard. store_bbs_3mf always writes
    // relationships pointing at plate_<n>.png / plate_<n>_small.png but only
    // emits the file bodies when StoreParams::thumbnail_data is provided. For
    // a CLI that does not render, the realistic source of those bytes is the
    // input archive itself, and the clone-and-mutate flow ensures the source
    // remains valid.
    passthrough_missing_thumbnails(s, tmp_path);

    // Run runtime invariant guards on the .tmp BEFORE renaming over the
    // destination. If any check fails, remove the .tmp and propagate so the
    // CLI maps the failure to ExitCode::invariant_violation. The destination
    // is untouched on failure.
    try {
        run_all_invariants(s, tmp_path);
    } catch (const InvariantViolation&) {
        boost::system::error_code ec;
        fs::remove(tmp, ec);
        throw;
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
