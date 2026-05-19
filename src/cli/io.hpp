#pragma once
#include "project_ops.hpp"
#include <string>

namespace orca_cli {

// Load a .3mf into a ProjectState. Throws std::runtime_error on failure.
// Internally calls libslic3r's load_bbs_3mf with LoadModel | LoadConfig and
// rebuilds PlateData::objects_and_instances from the loaded_id <-> obj_inst_map
// invariant (which Bambu / Orca readers populate but do not synthesize from).
ProjectState load_project(const std::string& path);

// Atomically save a ProjectState as a .3mf at target_path:
//   1. write to "<target_path>.tmp"
//   2. run all archive invariant guards (throws InvariantViolation on failure)
//   3. rename .tmp -> target_path
// Throws std::runtime_error on store failure; the .tmp file is removed in
// both failure paths.
void save_project(const ProjectState& state, const std::string& target_path);

} // namespace orca_cli
