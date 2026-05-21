#pragma once
#include "project_ops.hpp"
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace orca_cli {

// Thrown when a 3mf produced by save_project fails any of the runtime
// invariant checks. The CLI maps this to ExitCode::invariant_violation; the
// archive is left in the failed-write .tmp state (i.e. NOT renamed over the
// destination), so callers see the failure cleanly.
class InvariantViolation : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One file entry from an in-memory unzip pass.
struct ZipEntry {
    std::string       name;
    std::vector<char> bytes;
};

// Read every file from a .3mf (zip) into memory. Throws InvariantViolation
// if the archive cannot be opened. Directory entries are skipped.
std::vector<ZipEntry> unzip_to_memory(const std::string& zip_path);

// Runtime invariant guards. Each throws InvariantViolation on failure.
//
// 1) verify_relationships              -- every Target attribute in any
//    "*.rels" entry must point to a real entry inside the archive (catches
//    Bug B - dangling references that fail GUI plate-thumbnail lookup).
//
// 2) verify_plate_thumbnails           -- for every Metadata/plate_N.png,
//    there must also be a Metadata/plate_N_small.png. The GUI loads both.
//
// 3) verify_vector_config_roundtrip    -- compare every vector-typed option
//    in the in-memory project_config against the same option in a fresh
//    re-parse from disk. A mismatch indicates a save/load asymmetry for a
//    vector key (the class of bug that lost wipe_volume_matrix on the
//    first attempt of v1 of this CLI).
void verify_relationships         (const std::vector<ZipEntry>& entries);
void verify_plate_thumbnails      (const std::vector<ZipEntry>& entries);
void verify_vector_config_roundtrip(const ProjectState& in_memory,
                                    const std::string&  zip_path);

// Run all three checks in sequence on the produced archive. Called by
// save_project before the atomic .tmp -> target rename.
void run_all_invariants(const ProjectState& in_memory,
                        const std::string&  zip_path);

// Standalone variant of check #2 (verify_plate_thumbnails) tuned for the
// `project init` input-template sanity check (cross-project audit P2).
// `zip_path` is the file to inspect; `display_path` is what appears in
// user-facing error messages (the staging copy is an implementation
// detail; the user knows their --template argument). Throws
// InvariantViolation on either open failure or missing plate small-
// thumbnail; the CLI command layer maps to ExitCode::invariant_violation
// (exit 8) via the existing InvariantViolation catch in do_project_init.
void verify_input_template_thumbnails(const std::string& zip_path,
                                      const std::string& display_path);

// Lightweight central-directory enumeration: opens the zip, walks entry
// names, closes. Does NOT decompress entry bodies. Returns empty vector
// on open failure.
std::vector<std::string> enumerate_zip_entry_names(const std::string& zip_path);

// Extract a single entry by exact name. Returns nullopt if the entry is
// missing or the archive cannot be opened. Slashes in entry names are
// normalized to '/' (matching the rest of the CLI's zip handling).
std::optional<std::vector<unsigned char>>
extract_entry_to_memory(const std::string& zip_path, const std::string& entry_name);

} // namespace orca_cli
