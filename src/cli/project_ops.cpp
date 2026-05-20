// project_ops.cpp -- mutations on a loaded ProjectState. Phase 2 introduces
// the plate ops; Phase 3 adds object add / remove. Phase 6 adds the
// project / per-object config set/unset/list helpers.
#include "project_ops.hpp"
#include "placement.hpp"

#include <libslic3r/Format/STL.hpp>
#include <libslic3r/Preset.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <boost/filesystem.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace orca_cli {

Slic3r::ModelObject* find_object(ProjectState& s, const std::string& name) {
    for (auto* o : s.model->objects) if (o->name == name) return o;
    return nullptr;
}

const Slic3r::ModelObject* find_object(const ProjectState& s, const std::string& name) {
    for (const auto* o : s.model->objects) if (o->name == name) return o;
    return nullptr;
}

Slic3r::ModelObject& find_object_or_throw(ProjectState& s, const std::string& name) {
    if (auto* o = find_object(s, name)) return *o;
    throw std::out_of_range("object not found: " + name);
}

const Slic3r::ModelObject& find_object_or_throw(const ProjectState& s, const std::string& name) {
    if (auto* o = find_object(s, name)) return *o;
    throw std::out_of_range("object not found: " + name);
}

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
    BoundingBoxf bb(pa->values);
    bed = BoundingBoxf3(Vec3d(bb.min.x(), bb.min.y(), 0.0),
                        Vec3d(bb.max.x(), bb.max.y(), 0.0));
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

    // Per-plate origin offset (sqrt grid). Stride MUST match OrcaSlicer's
    // PartPlateList::plate_stride_{x,y}() which is m_plate_{width,depth} *
    // (1 + LOGICAL_PART_PLATE_GAP), where LOGICAL_PART_PLATE_GAP = 1/5
    // (src/slic3r/GUI/PartPlate.cpp:55). Using a fixed +10 mm gap instead
    // of this proportional stride silently puts grid-placed objects in
    // the inter-plate gap for any non-256 mm bed when multiple plates
    // are present.
    constexpr double GUI_PLATE_GAP_RATIO = 1.0 / 5.0;
    const double stride_x = (bed.max.x() - bed.min.x()) * (1.0 + GUI_PLATE_GAP_RATIO);
    const double stride_y = (bed.max.y() - bed.min.y()) * (1.0 + GUI_PLATE_GAP_RATIO);
    const Vec3d  plate_origin_offset_v = plate_origin_offset(
        plate_idx, int(s.plates.size()), stride_x, stride_y);
    const BoundingBoxf3 plate_bed(bed.min + plate_origin_offset_v,
                                  bed.max + plate_origin_offset_v);

    const int n       = std::max(1, p.count);
    const int obj_idx = int(s.model->objects.size() - 1);
    // bounding_box_exact() would force a mesh evaluation across every
    // existing instance; we only care about the volume extent for cell
    // sizing / AABB checks, so raw_mesh_bounding_box is enough and cheaper.
    const Vec3d bbox_size = obj->raw_mesh_bounding_box().size();

    if (has_explicit_transform(p)) {
        // Per spec § 4.3: "--count N stacks N copies at the same
        // post-transform position". All N instances share one offset.
        //
        // --translate is plate-local; world_offset folds in the plate's
        // origin in the GUI's plate-grid layout so a user asking for
        // (60,60) lands at (60,60) inside the named plate regardless of
        // which plate it is.
        const Vec3d local_offset = p.translate.value_or(Vec3d::Zero());
        const Vec3d world_offset = plate_bed.min + local_offset;

        // Off-bed AABB check.
        //
        // ASSUMPTION: the loaded STL's local origin coincides with its bbox center
        // (which is the case for STLs the GUI authors, and for our committed test
        // fixtures). For STLs with origin-at-corner, the world AABB used here is
        // shifted by half the extent from the true mesh AABB, so this check is
        // conservative for centered STLs and slightly off for corner-origin ones.
        // Matches the GUI's own off-bed indicator which uses the same approximation.
        //
        // Scale is folded into the half-extent so e.g. --scale 2 near a bed edge
        // trips the check. Rotation is intentionally NOT folded in (a rotated mesh's
        // AABB differs from the axis-aligned local-frame AABB; the GUI also doesn't
        // rotate-then-AABB for its off-bed indicator).
        const Vec3d scale_v = p.scale.value_or(Vec3d::Ones());
        const Vec3d scaled_half = Vec3d(bbox_size.x() * scale_v.x() * 0.5,
                                        bbox_size.y() * scale_v.y() * 0.5,
                                        bbox_size.z() * scale_v.z() * 0.5);
        const double aabb_min_x = world_offset.x() - scaled_half.x();
        const double aabb_max_x = world_offset.x() + scaled_half.x();
        const double aabb_min_y = world_offset.y() - scaled_half.y();
        const double aabb_max_y = world_offset.y() + scaled_half.y();
        if (aabb_min_x < plate_bed.min.x() || aabb_max_x > plate_bed.max.x() ||
            aabb_min_y < plate_bed.min.y() || aabb_max_y > plate_bed.max.y()) {
            throw PlacementFailure(
                "object AABB falls outside plate bed: translate=(" +
                std::to_string(local_offset.x()) + "," +
                std::to_string(local_offset.y()) + ") not within plate '" +
                p.plate_name + "'");
        }

        for (int i = 0; i < n; ++i) {
            ModelInstance* inst = obj->add_instance();
            inst->set_offset(world_offset);
            // libslic3r has no ModelObject::set_rotation(Vec3d) /
            // set_scaling_factor(Vec3d) -- only destructive ModelObject::
            // rotate/scale which alter the mesh. Per-instance is the
            // cleaner choice for "rotate/scale the loaded STL once" and
            // matches how the GUI represents object transforms. We copy
            // the same rotation/scale onto every stacked instance so the
            // behavior is identical regardless of which instance the GUI
            // picks as canonical.
            if (p.rotate.has_value())
                inst->set_rotation(*p.rotate);
            if (p.scale.has_value())
                inst->set_scaling_factor(*p.scale);
            plate->objects_and_instances.emplace_back(
                obj_idx, int(obj->instances.size() - 1));
        }
    } else {
        // No explicit transform: deterministic per-plate grid (P3).
        const int base_idx       = int(plate->objects_and_instances.size());
        // total_in_plate = pre-existing instance slots on this plate + the
        // n new ones we're about to add. Passing this to place_in_plate
        // freezes the grid width across the batch so prior slots don't
        // get re-mapped as slot indices grow (see placement.cpp comment).
        const int total_in_plate = base_idx + n;
        for (int i = 0; i < n; ++i) {
            ModelInstance* inst = obj->add_instance();
            inst->set_offset(place_in_plate(plate_bed, base_idx + i,
                                            total_in_plate, bbox_size));
            plate->objects_and_instances.emplace_back(
                obj_idx, int(obj->instances.size() - 1));
        }
    }

    // P5: stamp extruder if --filament was requested. Run after instance
    // placement so a failing slot validation does not leave a partially
    // placed half-object behind on the plate (set_object_filament throws
    // std::out_of_range -> exit 6 in commands/object.cpp). Source
    // attribution is already in place at this point, so a successful
    // --filament path preserves the Bug C defense -- the e2e test
    // assert_parts_have_source_file holds even when filament is set.
    if (p.filament_slot.has_value()) {
        set_object_filament(s, obj->name, *p.filament_slot);
    }
}

void set_object_filament(ProjectState& s, const std::string& object_name,
                         int filament_slot,
                         std::optional<std::string> part_name)
{
    using namespace Slic3r;

    // Locate the named ModelObject.
    ModelObject& obj = find_object_or_throw(s, object_name);

    // Determine the legal slot range from the project's filament_settings_id.
    // This is a ConfigOptionStrings whose .values.size() is the number of
    // filament profiles wired into the project. A reference 3mf for an
    // AMS-equipped Bambu printer typically has 4 or more slots; the
    // committed fixture has 6.
    int slot_count = 0;
    if (s.project_config) {
        if (const auto* fsid = s.project_config->option<ConfigOptionStrings>("filament_settings_id"))
            slot_count = int(fsid->values.size());
    }

    if (filament_slot < 1 || filament_slot > slot_count) {
        throw std::out_of_range(
            "filament slot " + std::to_string(filament_slot) +
            " out of range [1.." + std::to_string(slot_count) +
            "] for object '" + object_name + "'");
    }

    // When part_name is supplied and non-empty, write the extruder setting to
    // the named volume's per-volume config rather than the object-level config.
    // This is the T6 extension for split-to-parts workflows: individual parts
    // can be assigned different filament slots without affecting the object's
    // own config. We do NOT touch different_settings_to_system here -- that
    // field is for the GUI's preset diff, which is object-level only.
    if (part_name.has_value() && !part_name->empty()) {
        for (ModelVolume* v : obj.volumes) {
            if (v->name == *part_name) {
                v->config.set("extruder", filament_slot);
                return;
            }
        }
        throw std::out_of_range("part not found: " + *part_name);
    }

    // No part_name supplied (std::nullopt or empty): existing P5 object-level
    // write. ModelConfigObject::set<int>("extruder", N) constructs a
    // ConfigOptionInt under the hood (m_data.set(key, value, /*create=*/true))
    // and bumps the model-config timestamp. This is the same call-site shape
    // used by the GUI's MMU and "set extruder" paths (see
    // GUI_ObjectList.cpp:2819, ObjColorDialog.cpp:441) and by libslic3r itself
    // in Model.cpp:3066.
    obj.config.set("extruder", filament_slot);
}

// --------------------------------------------------------------------------
// Config mutations (P6).
//
// Shared helpers:
//   * validate_key_exists: throws BadConfigError if a key is not in
//     print_config_def. Used by all four set/unset helpers so a typo
//     surfaces consistently as exit 4 rather than landing in
//     ModelConfig::set_deserialize's "create a junk entry" path.
//   * find_object / find_object_or_throw: centralised in project_ops.cpp
//     at namespace scope (not anonymous) so all callers share one lookup.
//
// set_*: delegate the value parse to libslic3r's set_deserialize via a
// ConfigSubstitutionContext built with Disable (we want strict parsing
// for user-supplied values -- accepting silently-substituted enums on
// the CLI would defeat the bad_config exit code).

int filament_slot_count(const Slic3r::DynamicPrintConfig& cfg) {
    using namespace Slic3r;
    if (auto* fsid = cfg.option<ConfigOptionStrings>("filament_settings_id"))
        return std::max(1, int(fsid->values.size()));
    return 1;
}

namespace {

void validate_key_exists(const std::string& key)
{
    if (!Slic3r::print_config_def.has(key))
        throw BadConfigError("unknown config key: " + key);
}

// --------------------------------------------------------------------------
// different_settings_to_system marker helpers.
//
// The GUI's PresetBundle uses `different_settings_to_system` (a
// ConfigOptionStrings with filament_count + 2 slots) to identify which keys
// are project overrides vs preset defaults. Slot 0 is process-preset dirty
// keys, slot 1 is printer-preset dirty keys, slots 2..N+1 are per-filament
// dirty keys. Each slot's body is a c-style-escaped list of keys
// (escape_strings_cstyle / unescape_strings_cstyle).
//
// Without our key in that field, the GUI shows the preset's default value
// even though project_settings.config has our override -- the P6 smoke
// gate surfaced this with `sparse_infill_density: 30%` rendering as 15%.

// Decide which slot (process=0, printer=1, filament=2..) a config key belongs to.
// Returns -2 (sentinel) for filament keys meaning "all filament slots".
// Unknown keys default to slot 0 (process) so the GUI still sees them as
// overrides -- the GUI itself only renders them if they're meaningful to
// the process panel.
int classify_key_slot(const std::string& key, int /*filament_count*/)
{
    using namespace Slic3r;
    {
        const auto& opts = Preset::print_options();
        if (std::find(opts.begin(), opts.end(), key) != opts.end()) return 0;
    }
    {
        const auto& opts = Preset::printer_options();
        if (std::find(opts.begin(), opts.end(), key) != opts.end()) return 1;
    }
    {
        const auto& opts = Preset::filament_options();
        if (std::find(opts.begin(), opts.end(), key) != opts.end())
            return -2;   // sentinel: all filament slots
    }
    return 0;
}

// Returns the diff option sized to (filament_count + 2) slots, creating the
// option if missing.
Slic3r::ConfigOptionStrings* get_or_create_diff_settings(
    Slic3r::DynamicPrintConfig& cfg, int filament_count)
{
    using namespace Slic3r;
    auto* diff = cfg.option<ConfigOptionStrings>("different_settings_to_system", true);
    if (int(diff->values.size()) < filament_count + 2)
        diff->values.resize(size_t(filament_count + 2), std::string{});
    return diff;
}

void add_key_to_slot(Slic3r::ConfigOptionStrings* diff, int slot, const std::string& key)
{
    using namespace Slic3r;
    std::vector<std::string> keys;
    unescape_strings_cstyle(diff->values[slot], keys);
    if (std::find(keys.begin(), keys.end(), key) == keys.end())
        keys.push_back(key);
    diff->values[slot] = escape_strings_cstyle(keys);
}

void remove_key_from_slot(Slic3r::ConfigOptionStrings* diff, int slot, const std::string& key)
{
    using namespace Slic3r;
    std::vector<std::string> keys;
    unescape_strings_cstyle(diff->values[slot], keys);
    auto it = std::find(keys.begin(), keys.end(), key);
    if (it != keys.end()) keys.erase(it);
    diff->values[slot] = escape_strings_cstyle(keys);
}

void mark_project_key_dirty(ProjectState& s, const std::string& key)
{
    int fc = filament_slot_count(*s.project_config);
    auto* diff = get_or_create_diff_settings(*s.project_config, fc);
    int slot = classify_key_slot(key, fc);
    if (slot == -2) {
        for (int i = 0; i < fc; ++i)
            add_key_to_slot(diff, 2 + i, key);
    } else {
        add_key_to_slot(diff, slot, key);
    }
}

void unmark_project_key_dirty(ProjectState& s, const std::string& key)
{
    int fc = filament_slot_count(*s.project_config);
    auto* diff = s.project_config->option<Slic3r::ConfigOptionStrings>("different_settings_to_system");
    if (!diff) return;
    if (int(diff->values.size()) < fc + 2) return;
    int slot = classify_key_slot(key, fc);
    if (slot == -2) {
        for (int i = 0; i < fc; ++i)
            remove_key_from_slot(diff, 2 + i, key);
    } else {
        remove_key_from_slot(diff, slot, key);
    }
}

} // namespace

void set_project_config(ProjectState& s, const std::string& key, const std::string& value)
{
    using namespace Slic3r;
    validate_key_exists(key);
    // Disable: a value the user typed must parse strictly. We don't want
    // a typo'd enum to silently land as the option's first valid value.
    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Disable);
    try {
        s.project_config->set_deserialize(key, value, ctx);
    } catch (const std::exception& e) {
        throw BadConfigError("invalid value for '" + key + "': " + e.what());
    }
    // Mark the key in different_settings_to_system so the GUI's PresetBundle
    // treats it as a project override of the system preset rather than
    // falling back to the preset's default. Without this the value lands in
    // project_settings.config but the Process panel still renders the
    // preset's value (e.g. our 30% sparse_infill_density showed as 15%).
    mark_project_key_dirty(s, key);
}

void set_object_config(ProjectState& s, const std::string& object_name,
                       const std::string& key, const std::string& value)
{
    using namespace Slic3r;
    validate_key_exists(key);
    ModelObject& obj = find_object_or_throw(s, object_name);

    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Disable);
    try {
        // ModelConfigObject::set_deserialize bumps the model-config
        // timestamp on write -- same path the GUI uses when a per-object
        // setting changes through the settings panel.
        obj.config.set_deserialize(key, value, ctx);
    } catch (const std::exception& e) {
        throw BadConfigError("invalid value for '" + key + "': " + e.what());
    }
}

void unset_project_config(ProjectState& s, const std::string& key)
{
    validate_key_exists(key);
    // erase returns false if the key wasn't set; that's fine -- the
    // semantic is "key is not set after this call" rather than "removed
    // an existing key". A typo on the key name was already rejected
    // above by validate_key_exists.
    s.project_config->erase(key);
    // Symmetric with set_project_config: remove the key from
    // different_settings_to_system so the GUI no longer treats the
    // (now-removed) value as a project override of the preset.
    unmark_project_key_dirty(s, key);
}

void unset_object_config(ProjectState& s, const std::string& object_name,
                         const std::string& key)
{
    validate_key_exists(key);
    Slic3r::ModelObject& obj = find_object_or_throw(s, object_name);
    obj.config.erase(key);
}

std::vector<std::string> changed_project_keys(const ProjectState& s)
{
    using namespace Slic3r;
    // G6 (spec § 8): avoid `default_value->serialize()` here -- it crashes
    // on coEnums whose default holds an enum value. Instead, build a
    // defaults DynamicPrintConfig restricted to the same key set and let
    // DynamicConfig::diff do the comparison.
    auto keys = s.project_config->keys();
    // new_from_defaults_keys returns a new'd raw pointer; wrap to release.
    std::unique_ptr<DynamicPrintConfig> defaults(
        DynamicPrintConfig::new_from_defaults_keys(keys));
    return s.project_config->diff(*defaults);
}

std::vector<std::string> object_config_keys(const ProjectState& s,
                                            const std::string& object_name)
{
    const auto& obj = find_object_or_throw(s, object_name);
    // ModelConfig only ever holds the explicitly-set keys (the GUI
    // populates it lazily as the user touches settings), so this list IS
    // the change set -- no diff needed.
    return obj.config.keys();
}

std::vector<VolumeInfo> object_volume_info(const ProjectState& s,
                                           const std::string&  object_name)
{
    using namespace Slic3r;
    const ModelObject& obj = find_object_or_throw(s, object_name);

    int obj_extruder = 1;
    if (auto* oe = obj.config.get().opt<ConfigOptionInt>("extruder")) {
        obj_extruder = oe->value;
    }

    std::vector<VolumeInfo> out;
    out.reserve(obj.volumes.size());
    for (const ModelVolume* v : obj.volumes) {
        int eff = obj_extruder;
        if (auto* ve = v->config.get().opt<ConfigOptionInt>("extruder")) {
            eff = ve->value;
        }
        out.push_back({v->name, eff});
    }
    return out;
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

void stamp_source_if_missing(Slic3r::ModelVolume&              vol,
                             const Slic3r::ModelVolume::Source& fallback) {
    if (vol.source.input_file.empty()) {
        vol.source.input_file = fallback.input_file;
        vol.source.object_idx = fallback.object_idx;
        vol.source.volume_idx = fallback.volume_idx;
    }
}

void split_object_to_parts(ProjectState& s, const std::string& object_name) {
    using namespace Slic3r;
    ModelObject& obj = find_object_or_throw(s, object_name);

    if (obj.volumes.size() != 1) {
        throw std::invalid_argument(
            "cannot split: object already has multiple volumes; "
            "use object split-to-objects first");
    }
    ModelVolume* vol = obj.volumes.front();
    if (vol->type() != ModelVolumeType::MODEL_PART) {
        throw std::invalid_argument(
            "cannot split: only model parts can be split");
    }

    // Align volume name with object name so libslic3r's split-name
    // convention ({original}_1, _2, ...) produces user-visible names
    // matching the object name (not the STL filename). STL loader sets
    // vol->name = STL basename by default; without this alignment the
    // resulting parts would carry the filename prefix.
    vol->name = object_name;

    // Capture parent source BEFORE split. ModelVolume::split mutates
    // `*vol` in place (it becomes the first post-split volume), so we
    // need a snapshot to stamp any new volumes that ended up without
    // source attribution. Bug C defense - see stamp_source_if_missing.
    const ModelVolume::Source parent_source = vol->source;

    const int filament_count = filament_slot_count(*s.project_config);
    size_t produced = vol->split(static_cast<unsigned int>(filament_count),
                                 /*remap_paint=*/true);
    if (produced <= 1) {
        throw std::invalid_argument(
            "cannot split: object has only 1 connected mesh component");
    }

    for (ModelVolume* v : obj.volumes) {
        stamp_source_if_missing(*v, parent_source);
    }
}

void merge_object_parts(ProjectState& s,
                        const std::string&              object_name,
                        const std::vector<std::string>& source_part_names,
                        const std::string&              merged_part_name,
                        std::optional<int>              filament_override)
{
    using namespace Slic3r;

    // Section 3 precedence step 1 (parse-level usage_error cases 1-4) is
    // enforced at the CLI layer before this helper runs; here we trust
    // source_part_names.size() >= 2, no duplicates, merged_part_name
    // non-empty. The unit tests assert each precedence step in order.

    // Section 3 precedence step 2: locate object + sources.
    ModelObject& obj = find_object_or_throw(s, object_name);

    // Map source names to their indices in obj.volumes. Preserves the
    // user's --parts order via the indices vector, but the placement
    // algorithm uses lowest-existing-index regardless of argument order.
    std::vector<size_t> source_indices;
    source_indices.reserve(source_part_names.size());
    for (const auto& name : source_part_names) {
        size_t found = obj.volumes.size();
        for (size_t i = 0; i < obj.volumes.size(); ++i) {
            if (obj.volumes[i]->name == name) { found = i; break; }
        }
        if (found == obj.volumes.size()) {
            throw std::out_of_range(
                "source part not found on object '" + object_name +
                "': '" + name + "'");
        }
        source_indices.push_back(found);
    }

    // Section 3 precedence step 3: name collision (cases 8, 9).
    // --into must not collide with an existing volume name UNLESS the
    // colliding name is one of the sources being consumed (in which
    // case the existing volume is erased and its name is reused).
    {
        std::unordered_set<std::string> source_names(
            source_part_names.begin(), source_part_names.end());
        if (source_names.count(merged_part_name) == 0) {
            for (const ModelVolume* v : obj.volumes) {
                if (v->name == merged_part_name) {
                    throw DuplicateNameError(
                        "merged part name '" + merged_part_name +
                        "' collides with existing volume on object '" +
                        object_name + "' (not one of the sources)");
                }
            }
        }
    }

    // Section 3 precedence step 4: --filament range (case 7). Validate
    // here so an out-of-range value is reported as unknown_reference
    // (exit 6) BEFORE we run the source-type / empty-mesh / filament-
    // agreement checks (which would otherwise produce confusing
    // invalid_state errors when the user simply mistyped a slot number).
    if (filament_override.has_value()) {
        const int slot = *filament_override;
        const int max_slot = filament_slot_count(*s.project_config);
        if (slot < 1 || slot > max_slot) {
            throw std::out_of_range(
                "filament slot " + std::to_string(slot) +
                " out of range [1.." + std::to_string(max_slot) +
                "] for merge into '" + merged_part_name + "'");
        }
    }

    // Section 3 precedence step 5: source volume type (case 11).
    // Every source must be MODEL_PART. Merging modifier / support-
    // enforcer / etc. meshes is not meaningful in v1.
    for (size_t idx : source_indices) {
        if (obj.volumes[idx]->type() != ModelVolumeType::MODEL_PART) {
            throw std::invalid_argument(
                "cannot merge: source '" + obj.volumes[idx]->name +
                "' is not a model part (type=" +
                std::to_string(int(obj.volumes[idx]->type())) + ")");
        }
    }

    // Section 3 precedence step 6: empty-mesh handling (cases 12, 13).
    // Sources with empty meshes are silently dropped (matches
    // libslic3r's own ModelObject::merge() at Model.cpp:2181-2183).
    // After dropping, require >= 2 non-empty for a meaningful merge.
    std::vector<size_t> non_empty_indices;
    non_empty_indices.reserve(source_indices.size());
    for (size_t idx : source_indices) {
        if (!obj.volumes[idx]->mesh().empty()) {
            non_empty_indices.push_back(idx);
        }
    }
    if (non_empty_indices.empty()) {
        throw std::invalid_argument(
            "cannot merge: all source parts have empty meshes");
    }
    if (non_empty_indices.size() < 2) {
        throw std::invalid_argument(
            "cannot merge: merge requires >=2 non-empty source meshes "
            "(after dropping empty sources)");
    }

    // Bake-in transform + concat. Anchor = lowest existing index among
    // NON-EMPTY sources. An empty source has no geometry to anchor and
    // would produce a confusing inspect-output if it stole the slot.
    const size_t anchor_idx =
        *std::min_element(non_empty_indices.begin(), non_empty_indices.end());

    TriangleMesh merged_mesh;
    for (size_t idx : non_empty_indices) {
        ModelVolume* v = obj.volumes[idx];
        TriangleMesh m(v->mesh());
        m.transform(v->get_matrix(), /*fix_left_handed=*/true);
        merged_mesh.merge(m);
    }

    // Capture source attribution from the lowest-existing-index source
    // BEFORE we erase any volumes. Used to stamp the merged volume so
    // Bug C defense is preserved.
    const ModelVolume::Source anchor_source = obj.volumes[anchor_idx]->source;

    // Build the merged ModelVolume in place at the anchor position by
    // mutating obj.volumes[anchor_idx] and then erasing the OTHER
    // sources. ModelObject::add_volume returns a freshly-allocated
    // ModelVolume; we use it for the merged data, then splice it into
    // the anchor slot and delete the original anchor.
    ModelVolume* merged = obj.add_volume(merged_mesh, /*modify_to_center_geometry=*/false);
    merged->name = merged_part_name;
    merged->set_transformation(Geometry::Transformation());
    merged->source = anchor_source;

    // obj.add_volume pushed `merged` to the end of obj.volumes. Move it
    // to the anchor position, then erase the old anchor + the other
    // sources. Build the set of indices to erase (everything in
    // source_indices), then walk obj.volumes building the new vector
    // with `merged` inserted at the anchor slot.
    std::vector<ModelVolume*> rebuilt;
    rebuilt.reserve(obj.volumes.size() - source_indices.size());
    std::vector<bool> drop(obj.volumes.size(), false);
    for (size_t idx : source_indices) drop[idx] = true;
    // The freshly-added merged volume is at the end (last index) and is
    // NOT in source_indices, so drop[obj.volumes.size() - 1] == false.
    const size_t merged_pos_in_volumes = obj.volumes.size() - 1;
    drop[merged_pos_in_volumes] = true; // we'll re-insert at anchor

    for (size_t i = 0; i < obj.volumes.size(); ++i) {
        if (i == anchor_idx) {
            rebuilt.push_back(merged);
        } else if (!drop[i]) {
            rebuilt.push_back(obj.volumes[i]);
        }
        // else: source that is not the anchor, or the freshly-added
        // merged volume at the end -- both skipped here.
    }

    // Delete the original anchor + non-anchor sources whose pointers
    // are about to be orphaned (everything in source_indices except the
    // anchor's slot, plus the original anchor itself). The merged
    // volume's pointer is preserved in `rebuilt`.
    for (size_t idx : source_indices) {
        delete obj.volumes[idx];
    }
    obj.volumes.swap(rebuilt);
    obj.invalidate_bounding_box();

    // filament_override handling is added in Task 7. For now, the
    // merged volume inherits whatever extruder libslic3r assigned by
    // default. The happy-path test does not assert filament.
    (void)filament_override;
}

} // namespace orca_cli
