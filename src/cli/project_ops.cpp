// project_ops.cpp -- mutations on a loaded ProjectState. Phase 2 introduces
// the plate ops; Phase 3 adds object add / remove. Later phases will add
// transform / filament / config.
#include "project_ops.hpp"
#include "placement.hpp"

#include <libslic3r/Format/STL.hpp>

#include <boost/filesystem.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace orca_cli {

void add_plate(ProjectState& s, const std::string& name)
{
    for (auto& p : s.plates) {
        if (p->plate_name == name)
            throw std::invalid_argument("duplicate plate name: " + name);
    }

    auto pd = std::make_unique<Slic3r::PlateData>();
    pd->plate_name  = name;
    pd->plate_index = static_cast<int>(s.plates.size());
    // No thumbnail_file or thumbnail_data set here. passthrough_missing_thumbnails
    // in save_project injects placeholder PNG bytes for any plate whose
    // Metadata/plate_N.png and plate_N_small.png entries are missing from both
    // the source archive and the just-stored .tmp archive.
    s.plates.emplace_back(std::move(pd));
}

void remove_plate(ProjectState& s, const std::string& name)
{
    if (s.plates.size() <= 1)
        throw std::invalid_argument("cannot remove the only plate");

    auto it = std::find_if(
        s.plates.begin(), s.plates.end(),
        [&](const std::unique_ptr<Slic3r::PlateData>& p) {
            return p->plate_name == name;
        });
    if (it == s.plates.end())
        throw std::out_of_range("plate not found: " + name);

    s.plates.erase(it);
    // Re-index the remaining plates so plate_index stays contiguous from 0;
    // store_bbs_3mf and the GUI both treat plate_index as the canonical
    // 1-based position on disk (plate_N.png follows from plate_index + 1).
    for (size_t i = 0; i < s.plates.size(); ++i)
        s.plates[i]->plate_index = static_cast<int>(i);
}

void rename_plate(ProjectState& s, const std::string& from, const std::string& to)
{
    // Existence check first: `--from missing --to missing` must report the
    // missing-`from` error rather than silently no-op.
    Slic3r::PlateData* found = nullptr;
    for (auto& p : s.plates) {
        if (p->plate_name == from) found = p.get();
    }
    if (!found)
        throw std::out_of_range("plate not found: " + from);

    // Self-rename is a no-op AFTER existence is confirmed.
    if (from == to) return;

    // Duplicate-`to` check (excluding the plate we'd be renaming, though for
    // distinct from/to here that distinction is moot -- p->plate_name == to
    // cannot also equal from).
    for (auto& p : s.plates) {
        if (p.get() != found && p->plate_name == to)
            throw std::invalid_argument("duplicate plate name: " + to);
    }
    found->plate_name = to;
}

// --------------------------------------------------------------------------
// Object mutations (P3).

namespace {

// Read the project's printable_area as a 2D AABB (X/Y, Z forced to 0).
// Returns false if the option is missing or empty.
bool read_printable_area_aabb(const Slic3r::DynamicPrintConfig& cfg,
                              Slic3r::BoundingBoxf3&            bed)
{
    using namespace Slic3r;
    const auto* pa = cfg.option<ConfigOptionPoints>("printable_area");
    if (!pa || pa->values.empty()) return false;

    double minx =  std::numeric_limits<double>::infinity();
    double miny =  std::numeric_limits<double>::infinity();
    double maxx = -std::numeric_limits<double>::infinity();
    double maxy = -std::numeric_limits<double>::infinity();
    for (const Vec2d& pt : pa->values) {
        minx = std::min(minx, pt.x());
        maxx = std::max(maxx, pt.x());
        miny = std::min(miny, pt.y());
        maxy = std::max(maxy, pt.y());
    }
    bed = BoundingBoxf3(Vec3d(minx, miny, 0.0), Vec3d(maxx, maxy, 0.0));
    return true;
}

int find_plate_index(const ProjectState& s, const std::string& name)
{
    for (size_t i = 0; i < s.plates.size(); ++i)
        if (s.plates[i]->plate_name == name) return int(i);
    return -1;
}

// Bug C defense (spec section 8): every ModelVolume must have a
// source.input_file or some Orca / Bambu GUI versions silently drop the
// part on load. object_idx/volume_idx pair must also be set; pick (0, 0)
// for the canonical "first volume of first object in the STL" case.
// Unit + e2e tests assert this.
void stamp_source_attribution(Slic3r::ModelObject& obj, const std::string& stl_path)
{
    for (Slic3r::ModelVolume* vol : obj.volumes) {
        vol->source.input_file = stl_path;
        vol->source.object_idx = 0;
        vol->source.volume_idx = 0;
    }
}

} // namespace

void add_object(ProjectState& s, const AddObjectParams& p)
{
    using namespace Slic3r;

    int plate_idx = find_plate_index(s, p.plate_name);
    if (plate_idx < 0)
        throw std::out_of_range("plate not found: " + p.plate_name);
    PlateData* plate = s.plates[plate_idx].get();

    // Load the STL into a scratch Model so we can move the first object
    // into the project's model without disturbing other state. load_stl
    // returns false on failure and does not throw, which is friendlier for
    // mapping to ExitCode::parse_failure at the command layer.
    Model stl_model;
    if (!load_stl(p.stl_path.c_str(), &stl_model, /*object_name*/ nullptr))
        throw std::runtime_error("failed to load STL: " + p.stl_path);
    if (stl_model.objects.empty())
        throw std::runtime_error("STL produced no objects: " + p.stl_path);

    // add_object(const ModelObject&) deep-copies the volumes and re-issues
    // ObjectIDs so the new object is independent of stl_model (which goes
    // out of scope at end of function).
    ModelObject* obj = s.model->add_object(*stl_model.objects.front());
    obj->name = p.object_name.empty()
        ? boost::filesystem::path(p.stl_path).stem().string()
        : p.object_name;
    obj->input_file = p.stl_path;

    stamp_source_attribution(*obj, p.stl_path);

    // The reference 3mfs already have at least one instance per copied
    // ModelObject (add_object(const ModelObject&) preserves the original
    // instance vector). Drop those so we control instance count entirely
    // via `count` below -- prevents off-by-one when the source STL had a
    // pre-existing instance.
    obj->clear_instances();

    BoundingBoxf3 bed;
    if (!read_printable_area_aabb(*s.project_config, bed)) {
        // Safe default that covers every printer we ship. Only hit when
        // the project was loaded without a printable_area config option
        // -- shouldn't happen for reference 3mfs but we guard anyway.
        bed = BoundingBoxf3(Vec3d(0, 0, 0), Vec3d(250, 250, 250));
    }

    // Per-plate origin offset (sqrt grid). Strides = bed extent + a 10 mm
    // gap so neighboring plates don't visually overlap in the GUI grid.
    const double stride_x = (bed.max.x() - bed.min.x()) + 10.0;
    const double stride_y = (bed.max.y() - bed.min.y()) + 10.0;
    const Vec3d  offset   = plate_origin_offset(
        plate_idx, int(s.plates.size()), stride_x, stride_y);
    const BoundingBoxf3 plate_bed(bed.min + offset, bed.max + offset);

    const int   base_idx       = int(plate->objects_and_instances.size());
    const int   n              = std::max(1, p.count);
    // total_in_plate = pre-existing instance slots on this plate + the
    // n new ones we're about to add. Passing this to place_in_plate
    // freezes the grid width across the batch so prior slots don't get
    // re-mapped as slot indices grow (see placement.cpp comment).
    const int   total_in_plate = base_idx + n;
    // bounding_box_exact() would force a mesh evaluation across every
    // existing instance; we only care about the volume extent for cell
    // sizing, so raw_mesh_bounding_box is enough and cheaper.
    const Vec3d bbox_size = obj->raw_mesh_bounding_box().size();
    const int   obj_idx   = int(s.model->objects.size() - 1);

    for (int i = 0; i < n; ++i) {
        ModelInstance* inst = obj->add_instance();
        inst->set_offset(place_in_plate(plate_bed, base_idx + i, total_in_plate, bbox_size));
        plate->objects_and_instances.emplace_back(obj_idx,
            int(obj->instances.size() - 1));
    }
}

void remove_object(ProjectState& s, const std::string& object_name)
{
    using namespace Slic3r;

    auto it = std::find_if(s.model->objects.begin(), s.model->objects.end(),
        [&](ModelObject* o) { return o->name == object_name; });
    if (it == s.model->objects.end())
        throw std::out_of_range("object not found: " + object_name);

    const int removed_idx = int(it - s.model->objects.begin());
    s.model->delete_object(size_t(removed_idx));

    // Rebuild every plate's objects_and_instances:
    //   - drop entries pointing at the removed object,
    //   - shift indices > removed_idx down by 1 so they continue to
    //     refer to the same ModelObject after the erase.
    for (auto& pd : s.plates) {
        std::vector<std::pair<int, int>> kept;
        kept.reserve(pd->objects_and_instances.size());
        for (const auto& kv : pd->objects_and_instances) {
            const int oi = kv.first;
            const int ii = kv.second;
            if (oi == removed_idx) continue;
            kept.emplace_back(oi > removed_idx ? oi - 1 : oi, ii);
        }
        pd->objects_and_instances.swap(kept);
    }
}

} // namespace orca_cli
