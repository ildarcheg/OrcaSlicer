// src/cli/project_tab_ops.hpp
#pragma once
#include "project_ops.hpp"            // ProjectState
#include <boost/filesystem/path.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace orca_cli {

// -----------------------------------------------------------------------------
// Exception types (spec § 5.1).
// All are std::runtime_error subclasses so MutationExceptionMap can dispatch
// them ahead of the std::invalid_argument / std::out_of_range defaults.

// `--cover IMG` is not a valid PNG (missing, unreadable, or first 8 bytes do
// not match the PNG signature 89 50 4E 47 0D 0A 1A 0A). Mapped to
// ExitCode::bad_config (exit 4).
class BadCoverImage : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};

// `--file PATH` for `aux add` is missing or unreadable. Distinct from
// BadCoverImage so the caller can emit a targeted message. Mapped to
// ExitCode::file_not_found (exit 2).
class BadAuxFile : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};

// `info clear --field X` (or `profile clear --field X`) where X is not in
// the per-surface allowed set. Mapped to ExitCode::bad_config (exit 4).
class InvalidField : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};

// `aux add --name N` rejected by sanitization (path separators, Windows
// reserved names, etc. — see sanitize_aux_name). Mapped to
// ExitCode::bad_config (exit 4).
class AuxNameError : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};

// `aux add` target name already exists in the bucket and --force is not set.
// Mapped to ExitCode::duplicate_name (exit 5). Mirrors DuplicateNameError
// from v4 (merge-parts) as the canonical "name taken" signal.
class AuxCollisionError : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};

// -----------------------------------------------------------------------------
// Auxiliary folder enum (spec § 2.1).
// The four GUI-visible buckets in OrcaSlicer's AuxiliaryPanel
// (src/slic3r/GUI/Auxiliary.cpp:866-876). String mapping for the CLI flag
// and JSON key is centralised in folder_flag() / folder_json_key().

enum class AuxFolder {
    pictures,        // --folder pictures        | JSON key "pictures"
    bom,             // --folder bom             | JSON key "bom"
    assembly_guide,  // --folder assembly-guide  | JSON key "assembly_guide"
    others,          // --folder others          | JSON key "others"
};

// Hyphen-form for the CLI flag (Unix convention).
const char* folder_flag(AuxFolder f);
// Underscore-form for the JSON key (data convention — spec § 2.3).
const char* folder_json_key(AuxFolder f);
// In-3mf subdirectory under Metadata/Auxiliaries/, exactly matching the
// directory name the OrcaSlicer GUI uses (see src/slic3r/GUI/Auxiliary.cpp).
const char* folder_subdir(AuxFolder f);

// Cover-image target selector for embed_cover_image / clear_cover_image.
enum class CoverTarget { Info, Profile };

// -----------------------------------------------------------------------------
// ModelInfo (model-level) ops (spec § 4 "project info show / set / clear").

// View of the ModelInfo fields surfaced in `project info show`. All strings
// are empty when model.model_info is nullptr or the field is unset.
struct InfoView {
    std::string title;        // ModelInfo::model_name
    std::string description;  // ModelInfo::description
    std::string license;      // ModelInfo::license
    std::string copyright;    // ModelInfo::copyright
    std::string cover;        // ModelInfo::cover_file
    std::string origin;       // ModelInfo::origin   (read-only)
};

InfoView info_view(const ProjectState& s);

// Field selectors used by info_set / info_clear and validated by
// allowed_info_fields(). Aliasing keeps the public API typed.
struct InfoSetParams {
    std::optional<std::string>             title;
    std::optional<std::string>             description;
    std::optional<std::string>             license;
    std::optional<std::string>             copyright;
    std::optional<boost::filesystem::path> cover;  // PNG source path
};

// Returns true iff at least one field is set. Used by the command layer to
// reject zero-field invocations before calling the op (CLI11 also enforces
// this, but a defensive check guards direct unit-test callers).
bool any_field_set(const InfoSetParams& p);

// Apply each set field to model.model_info (allocating it if nullptr).
// --cover routes through embed_cover_image (PNG-only; throws BadCoverImage
// on non-PNG content).
//
//   throws BadCoverImage if p.cover is set and its first 8 bytes are not
//                        the PNG signature, or if the source is unreadable.
void info_set(ProjectState& s, const InfoSetParams& p);

// Allowed --field names for info clear (spec § 2.1).
const std::vector<std::string>& allowed_info_fields();

// Null each named field. `cover` routes through clear_cover_image (refcount —
// see spec § 3.5). Unknown name -> InvalidField. Idempotent.
//
//   throws InvalidField if any name in `fields` is not in
//                       allowed_info_fields().
void info_clear(ProjectState& s, const std::vector<std::string>& fields);

// -----------------------------------------------------------------------------
// ModelProfileInfo ops (spec § 4 "project profile show / set / clear").

struct ProfileView {
    std::string title;        // ProfileInfo::ProfileTile  (sic)
    std::string description;  // ProfileInfo::ProfileDescription
    std::string cover;        // ProfileInfo::ProfileCover
    std::string user_id;      // ProfileInfo::ProfileUserId    (read-only)
    std::string user_name;    // ProfileInfo::ProfileUserName  (read-only)
};

ProfileView profile_view(const ProjectState& s);

struct ProfileSetParams {
    std::optional<std::string>             title;
    std::optional<std::string>             description;
    std::optional<boost::filesystem::path> cover;
};

bool any_field_set(const ProfileSetParams& p);
void profile_set(ProjectState& s, const ProfileSetParams& p);

const std::vector<std::string>& allowed_profile_fields();
void profile_clear(ProjectState& s, const std::vector<std::string>& fields);

// -----------------------------------------------------------------------------
// Aux file ops (spec § 4 "project aux …").

struct AuxEntry {
    AuxFolder   folder;
    std::string name;       // in-3mf basename (no path)
    std::uint64_t size;     // bytes
};

// Walks all four bucket subdirs under model.get_auxiliary_file_temp_path().
// Returns four-key result; each key always present (empty vector when bucket
// is missing or empty). Matches spec § 2.2 "stable shape" rule.
std::vector<AuxEntry> aux_list(const ProjectState& s);

struct AuxAddParams {
    AuxFolder                       folder;
    boost::filesystem::path         file;     // source on disk
    std::optional<std::string>      name;     // override basename
    bool                            force = false;
};

// Copies the source file into the named bucket inside the model's auxiliary
// temp dir. Sanitizes the in-3mf basename via sanitize_aux_name. Collision
// (target name already present) -> AuxCollisionError unless --force.
//
//   throws BadAuxFile       if p.file is missing or unreadable.
//   throws AuxNameError     if the computed in-3mf basename is unsafe.
//   throws AuxCollisionError if the target name is taken and p.force == false.
//                            --force on a byte-identical file is a no-op
//                            (still returns success).
void aux_add(ProjectState& s, const AuxAddParams& p);

// Removes the named entry from the named bucket.
//   throws std::out_of_range if no entry with that name in that bucket.
void aux_remove(ProjectState& s, AuxFolder folder, const std::string& name);

// Exports the named entry to the user's filesystem path. If `to` is an
// existing directory the file is written to `to / name`; otherwise `to` is
// the destination file path. Existing destinations are overwritten without
// prompting (consent via explicit --to).
//
//   throws std::out_of_range     if no entry with that name in that bucket.
//   throws std::invalid_argument if the destination's parent dir does not
//                                exist (we do not create intermediate dirs).
//   throws std::runtime_error    on filesystem copy failure (passes through
//                                the OS error message).
void aux_export(const ProjectState&         s,
                AuxFolder                   folder,
                const std::string&          name,
                const boost::filesystem::path& to);

// -----------------------------------------------------------------------------
// Cover-image helpers (spec § 3.4, § 3.5). Exposed for unit tests.

// Reads first 8 bytes of `p`; returns true iff they exactly match the PNG
// signature 89 50 4E 47 0D 0A 1A 0A.
bool is_png(const boost::filesystem::path& p);

// PNG-only cover embed. Validates magic, copies bytes to
// <auxiliary_temp_dir>/.thumbnails/thumbnail_3mf.png, then sets either
// model.model_info->cover_file or model.profile_info->ProfileCover to
// "Auxiliaries/.thumbnails/thumbnail_3mf.png" (no leading slash; matches
// bbs_3mf.cpp:6802). Allocates model_info / profile_info if nullptr.
//
//   throws BadCoverImage if src is missing, unreadable, or not PNG.
void embed_cover_image(ProjectState&                  s,
                       const boost::filesystem::path& src,
                       CoverTarget                    target);

// Refcount-style clear. Nulls the named surface's pointer; deletes the
// embedded image file under <auxiliary_temp_dir>/.thumbnails/ only when the
// OTHER surface's pointer is ALSO empty after the clear. Idempotent: nulling
// an already-empty pointer is a no-op; deleting an already-missing file is
// a no-op. See spec § 2.1 worked example and § 3.5 numbered steps.
void clear_cover_image(ProjectState& s, CoverTarget target);

// -----------------------------------------------------------------------------
// Name sanitization (spec § 2.1).

// Rejects: empty; contains '/', '\\', '\0'; equals '.' or '..'; leading or
// trailing whitespace; leading or trailing '.'; Windows reserved device
// names CON / PRN / AUX / NUL / COM1-9 / LPT1-9 (case-insensitive, with or
// without extension — e.g. "con.png" is rejected too).
//
//   throws AuxNameError with a message naming the offending substring.
//
// On success returns the input unchanged (so callers can write
// `std::string clean = sanitize_aux_name(raw);`).
std::string sanitize_aux_name(const std::string& name);

} // namespace orca_cli
