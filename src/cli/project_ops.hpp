#pragma once
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/Format/bbs_3mf.hpp>
#include <memory>
#include <vector>

namespace orca_cli {

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

} // namespace orca_cli
