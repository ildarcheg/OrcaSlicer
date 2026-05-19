#pragma once
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/Format/bbs_3mf.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace orca_cli {

// Thrown by add_object when an explicit transform places the object's
// world-space AABB outside the target plate's bed (X or Y). The CLI maps
// this to ExitCode::placement_failure (exit 9). Distinct from
// std::out_of_range (which maps to ExitCode::unknown_reference, exit 6)
// so off-bed errors don't get reported as "plate not found".
//
// Mirrors the InvariantViolation pattern from invariants.hpp.
class PlacementFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};


// In-memory representation of a loaded 3mf project.
// Owns the Model, the project DynamicPrintConfig, and the PlateData vector
// (which describes plate <-> object-instance mappings).
struct ProjectState {
    std::unique_ptr<Slic3r::Model>                  model;
    std::unique_ptr<Slic3r::DynamicPrintConfig>     project_config;
    std::vector<std::unique_ptr<Slic3r::PlateData>> plates;

    // Path to the .3mf that load_project() read this state from. Empty if
    // the state was constructed from scratch. save_project() uses this to
    // copy through binary blobs that the libslic3r store path emits a
    // relationship for but does not re-encode (notably plate thumbnails:
    // load_bbs_3mf reads them as raw PNG bytes into PlateData::plate_thumbnail
    // .pixels, but store_bbs_3mf only writes thumbnails when the caller
    // provides decoded RGBA via StoreParams::thumbnail_data). For
    // clone-and-mutate flows like `project init` we just want to pass these
    // blobs through verbatim so the resulting archive's relationships file
    // does not dangle. See save_project() in src/cli/io.cpp.
    //
    // LIFETIME CONTRACT: the file at source_path must remain readable
    // between load_project and save_project -- the thumbnail passthrough
    // re-opens the source archive to copy plate PNGs forward. If the source
    // is deleted, save_project will produce an archive that fails the
    // runtime invariant guard with a dangling-relationship error.
    std::string source_path;

    // View suitable for passing to libslic3r APIs that take PlateDataPtrs.
    Slic3r::PlateDataPtrs plate_data_ptrs() const {
        Slic3r::PlateDataPtrs r;
        r.reserve(plates.size());
        for (auto& p : plates) r.push_back(p.get());
        return r;
    }
};

// --------------------------------------------------------------------------
// Plate mutations (P2).
//
// All mutate ProjectState in place and throw on invalid inputs. The CLI
// command callbacks (src/cli/commands/plate.cpp) catch the exceptions and
// map them to ExitCode::{duplicate_name,unknown_reference,invalid_state}.
//
// add_plate: append a new plate with `name`. No thumbnail bytes are stored
// here -- the save passthrough (passthrough_missing_thumbnails in io.cpp)
// injects a placeholder PNG for any plate whose thumbnails don't already
// exist in the source archive.
//
//   throws std::invalid_argument if `name` matches an existing plate.
void add_plate(ProjectState& s, const std::string& name);

// remove_plate: remove the first plate whose plate_name == `name`. After
// removal, plate_index of the remaining plates is re-numbered contiguously
// from 0 so the on-disk plate_N.png naming stays consistent.
//
//   throws std::invalid_argument if this would leave zero plates.
//   throws std::out_of_range     if no plate with that name exists.
void remove_plate(ProjectState& s, const std::string& name);

// rename_plate: update the plate_name of the plate with name == `from` to
// `to`. plate_index is preserved. PNG thumbnail entries are NOT renamed;
// they are keyed by plate_index, not plate_name.
//
//   throws std::invalid_argument if a plate with name == `to` already exists.
//   throws std::out_of_range     if no plate with name == `from` exists.
void rename_plate(ProjectState& s, const std::string& from, const std::string& to);

// --------------------------------------------------------------------------
// Object mutations (P3).

// Parameters for add_object. P4 adds optional translate / rotate / scale;
// P5 adds optional filament_slot (1-based extruder index).
//
// Transform semantics (spec § 4.3):
//   translate: x,y[,z] in mm, plate-local (0,0 == plate's bed-min corner).
//              The 2-component form sets z=0.
//   rotate:    ax,ay,az Euler angles in radians, applied per-instance as
//              ModelInstance::set_rotation(Vec3d) -- world-space rotation
//              of the instance about its origin.
//   scale:     sx,sy,sz per-axis scaling factors applied per-instance via
//              ModelInstance::set_scaling_factor(Vec3d). The CLI accepts a
//              uniform scalar `s` which the parser expands to {s,s,s}.
//
// When any of {translate,rotate,scale} is set, add_object switches from
// the deterministic grid placement of P3 to "stacking": all `count`
// instances share the same post-transform offset. has_explicit_transform()
// captures that branch condition.
//
// filament_slot is independent of the transform branch -- when present,
// add_object stamps `extruder = filament_slot` on the new ModelObject's
// ModelConfigObject AFTER instance placement, by delegating to
// set_object_filament(). Out-of-range slots throw std::out_of_range.
struct AddObjectParams {
    std::string plate_name;     // target plate (must already exist)
    std::string stl_path;       // path to the STL on disk; also stamped as source
    std::string object_name;    // optional; defaults to STL basename
    int                          count = 1;   // number of instances on the target plate
    std::optional<Slic3r::Vec3d> translate;   // plate-local (mm)
    std::optional<Slic3r::Vec3d> rotate;      // Euler XYZ (radians)
    std::optional<Slic3r::Vec3d> scale;       // per-axis; uniform `s` -> {s,s,s}
    std::optional<int>           filament_slot; // 1-based; validated against filament_settings_id
};

inline bool has_explicit_transform(const AddObjectParams& p) {
    return p.translate.has_value() || p.rotate.has_value() || p.scale.has_value();
}

// add_object: load `p.stl_path` as a fresh ModelObject, append it to the
// project's model, stamp ModelVolume::source with the STL path + a
// well-defined object/volume index pair (Bug C defense -- some Orca/Bambu
// GUIs silently drop objects whose volumes lack a source.input_file), and
// place `p.count` instances on the named plate via the deterministic grid
// math in placement.hpp.
//
//   throws std::out_of_range  if `p.plate_name` is not an existing plate.
//   throws std::runtime_error if the STL cannot be loaded.
//   throws PlacementFailure   if has_explicit_transform(p) and the
//                             resulting world-space AABB falls outside
//                             the plate's bed (X/Y).
void add_object(ProjectState& s, const AddObjectParams& p);

// remove_object: remove the first ModelObject whose name matches and rebuild
// every plate's objects_and_instances map so that
//   - entries pointing at the removed object are dropped, and
//   - object indices greater than the removed index are shifted down by 1.
//
//   throws std::out_of_range if no object with that name exists.
void remove_object(ProjectState& s, const std::string& object_name);

// --------------------------------------------------------------------------
// Filament slot assignment (P5).
//
// set_object_filament: stamp `extruder = filament_slot` on the named
// ModelObject's per-object config. The filament slot is a 1-based index
// into the project's filament_settings_id (a ConfigOptionStrings whose
// .values.size() determines the legal range).
//
//   throws std::out_of_range if no object with that name exists.
//   throws std::out_of_range if filament_slot < 1 or
//          filament_slot > filament_settings_id.values.size().
//
// Both error modes share std::out_of_range so the command catch chain
// in commands/object.cpp maps the whole class of "unknown reference"
// failures (unknown plate, unknown object, out-of-range slot) to
// ExitCode::unknown_reference (exit 6) -- no new exit code required.
void set_object_filament(ProjectState& s, const std::string& object_name, int filament_slot);

} // namespace orca_cli
