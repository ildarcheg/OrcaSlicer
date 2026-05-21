# orca-cli `project info|profile|aux` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add three new subcommand families to `orca-cli project`:
- `project info {show,set,clear}` — model-level metadata (title, description, license, copyright, cover image).
- `project profile {show,set,clear}` — print-profile metadata (title, description, cover image, read-only user_id/user_name).
- `project aux {list,add,remove,export}` — auxiliary file attachments in four GUI-visible buckets (`pictures`, `bom`, `assembly-guide`, `others`).

After this ships, an automation pipeline can produce a 3mf that opens in OrcaSlicer with a fully populated Project tab (model + profile metadata + supporting documents) without any GUI touch.

**Architecture:** All ops live in a new `src/cli/project_tab_ops.{hpp,cpp}` to avoid bloating `project_ops.cpp` (already 986 lines). CLI dispatch lives in a new `src/cli/commands/project_tab.{hpp,cpp}` that installs the three subverb families under the existing `project` parent. `commands/project_init.cpp` is refactored so the parent `project` subcommand is created once and each leaf-installer attaches its own children. All mutating verbs route through the existing v2 clone-and-mutate envelope (`run_mutation` → `load_project` → mutate → `save_project` → invariant guard → atomic rename). No new invariant is added.

**Spec:** `docs/superpowers/specs/2026-05-20-orca-cli-project-tab-design.md` (approved at `94fd5c6d51`). The spec defines the command surface (§ 2), shared-cover refcount semantics (§ 2.1, § 3.5), PNG-only embedding (§ 3.4), exit-code mapping to v2's `ExitCode` enum (§ 5.1), and the test plan (§ 6, ~40 new cases).

**Tech Stack:** C++17, libslic3r, miniz (via libslic3r's `miniz_extension`), nlohmann::json, Catch2 v3 (`<catch2/catch_all.hpp>`), CMake/CTest, CLI11.

**Build & test commands** (Windows, PowerShell):

```powershell
cmake --build build --config Release --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C Release
```

Faster iteration on the new tag:

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][project-tab]" --order rand --warn NoAssertions
```

**Test conventions** (per `tests/CLAUDE.md` + the v4 merge-parts ship):
- Catch2 v3 (`<catch2/catch_all.hpp>`).
- No `&&` inside `REQUIRE` — split into separate `REQUIRE`s.
- `DYNAMIC_SECTION` in loops, not `SECTION`.
- All new tests tagged `[orca-cli][project-tab]`; e2e also `[e2e]`; roundtrip also `[roundtrip]`.

**Baseline before this plan:** `main` is at `94fd5c6d51` (v4 merge-parts shipped + spec for this plan committed). The implementer must capture the exact `cli_tests.exe` baseline count in Task 0 before starting.

---

## File Structure Overview

| File | What it does | Action |
|------|--------------|--------|
| `src/cli/project_tab_ops.hpp` | Declare `info_set`/`info_clear`/`profile_set`/`profile_clear`/`aux_add`/`aux_remove`/`aux_list`/`aux_export` plus the `BadCoverImage`/`BadAuxFile`/`InvalidField`/`AuxNameError`/`AuxCollisionError` exception classes and the `AuxFolder` enum. | **Create** |
| `src/cli/project_tab_ops.cpp` | Implement all of the above plus internal helpers (`embed_cover_image`, `clear_cover_image`, `sanitize_aux_name`, `folder_subdir`, PNG signature check). | **Create** |
| `src/cli/commands/project_tab.hpp` | Declare `install_project_info_subcmd`, `install_project_profile_subcmd`, `install_project_aux_subcmd`. | **Create** |
| `src/cli/commands/project_tab.cpp` | Wire all 10 leaf verbs under the existing `project` parent. Each mutating verb uses `run_mutation` + `MutationExceptionMap`; read-only verbs (`show`, `list`, `export`) have their own catch chain. | **Create** |
| `src/cli/commands/project_init.hpp` | Expose `install_project_init_subcmd(CLI::App& project, GlobalOpts& g)` so `register_project_subcmd` can delegate. | **Modify** |
| `src/cli/commands/project_init.cpp` | Refactor `register_project_subcmd` to create the parent and call all four leaf-installers (init + info + profile + aux). Body of the existing init wiring moves into `install_project_init_subcmd`. | **Modify** |
| `src/cli/CMakeLists.txt` | Add `project_tab_ops.cpp` and `commands/project_tab.cpp` to the `orca_cli_core` static library sources. | **Modify** |
| `tests/cli/CMakeLists.txt` | Add `unit/test_project_tab_ops.cpp`, `e2e/test_project_tab.cpp`, `roundtrip/test_project_tab.cpp` to the `cli_tests` target. | **Modify** |
| `tests/cli/unit/test_project_tab_ops.cpp` | ~24 unit cases per spec § 6.1. | **Create** |
| `tests/cli/e2e/test_project_tab.cpp` | ~13 e2e cases per spec § 6.2. | **Create** |
| `tests/cli/roundtrip/test_project_tab.cpp` | 3 roundtrip cases per spec § 6.3. | **Create** |
| `tests/cli/fixtures/cover_smoke.png` | Tiny valid PNG (1×1 transparent, ~200 bytes). Happy-path cover fixture. | **Create (binary)** |
| `tests/cli/fixtures/cover_smoke.jpg` | Tiny valid JPG (1×1, ~300 bytes). Rejection-path fixture (exercises `BadCoverImage`). | **Create (binary)** |
| `tests/cli/fixtures/assembly_smoke.txt` | Plain UTF-8 text (~400 bytes). Aux happy-path fixture; chosen over a hand-rolled minimal PDF for byte-offset-fragility reasons documented in Task 22 Step 3. | **Create** |
| `docs/cli/manual-test.md` | Append **Phase 10** section per spec § 6.4. | **Modify** |
| `docs/cli/status.md` | Add Phase 10 status block (if the file exists; otherwise skip). | **Modify** |

No changes to `src/cli/main.cpp` (it still calls `register_project_subcmd`); no changes to `src/cli/output.hpp` (no new `ExitCode` values — spec § 5.1 maps every new failure to an existing label).

---

## Task 0: Capture baseline test count

**Files:** none (verification only).

- [ ] **Step 1: Build the test binary**

```powershell
cmake --build build --config Release --target cli_tests -- -m
```

Expected: `cli_tests.exe` builds cleanly.

- [ ] **Step 2: Run the existing suite and capture the count**

```powershell
& "build\tests\cli\Release\cli_tests.exe" --order rand --warn NoAssertions
```

Expected output ends with something like:
```
All tests passed (NNNNN assertions in MMM test cases)
```

Record both `MMM` (test case count) and `NNNNN` (assertion count) in this plan's PR description — Task 25 will verify the deltas (+~40 cases) are exactly what landed.

- [ ] **Step 3: No commit**

Baseline capture is observational; nothing changes on disk.

---

## Task 1: Add new header `project_tab_ops.hpp` with declarations and exception classes

**Files:**
- Create: `src/cli/project_tab_ops.hpp`

The header is API-surface only; the .cpp comes in Task 2. This task establishes the types so every later task can refer to them concretely.

- [ ] **Step 0: Verify the libslic3r types the plan assumes (must be done BEFORE editing the header)**

The plan assumes:
- `Slic3r::Model::model_info` is `std::shared_ptr<Slic3r::ModelInfo>`
- `Slic3r::Model::profile_info` is `std::shared_ptr<Slic3r::ModelProfileInfo>`
- `Slic3r::ModelInfo` has public `std::string model_name`
- `Slic3r::ModelProfileInfo` has public `std::string ProfileTile` (sic — typo preserved upstream), `ProfileCover`, `ProfileDescription`, `ProfileUserId`, `ProfileUserName`

Confirm each before proceeding. Three quick greps:

```powershell
# 1. shared_ptr members on Model
Select-String -Path src/libslic3r/Model.hpp -Pattern "shared_ptr<ModelInfo>|shared_ptr<ModelProfileInfo>"
# Expected (Model.hpp around line 1540-1542):
#   std::shared_ptr<ModelInfo> model_info = nullptr;
#   std::shared_ptr<ModelProfileInfo> profile_info = nullptr;

# 2. ModelInfo field names
Select-String -Path src/libslic3r/Model.hpp -Pattern "model_name|cover_file" -Context 0,0
# Expected: both names appear as `std::string` members of class ModelInfo
# around line 1494-1516.

# 3. ModelProfileInfo field names (note the typo "ProfileTile")
Select-String -Path src/libslic3r/Model.hpp -Pattern "ProfileTile|ProfileCover|ProfileUserId|ProfileUserName"
# Expected: all four appear as `std::string` members of class
# ModelProfileInfo around line 1475-1483. ProfileTile is the title field
# (typo preserved upstream).
```

If any assumption fails (e.g. `unique_ptr` instead of `shared_ptr`, or a different field name), **STOP** and reconcile before writing the header. The most likely shape changes:
- If members are `unique_ptr`, change every `std::make_shared<Slic3r::ModelInfo>()` in later tasks to `std::make_unique<Slic3r::ModelInfo>()`. The `nullptr` checks (`if (!s.model->model_info)`) continue to work identically.
- If `model_name` is named differently (e.g. `title`), update Task 5's `info_view` body and Task 6's `info_set` body to match. Search the plan for `model_name` and rename every occurrence consistently.

- [ ] **Step 1: Create the header file with the full declared surface**

```cpp
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
```

- [ ] **Step 2: Compile-check the header by including it from an empty test**

Add a temporary include at the top of `tests/cli/unit/test_project_ops.cpp`:

```cpp
#include "project_tab_ops.hpp"  // TEMP: verifies header compiles in isolation
```

Build:

```powershell
cmake --build build --config Release --target cli_tests -- -m
```

Expected: FAIL — `project_tab_ops.cpp` not yet wired into CMake, so symbols don't link yet, but the header itself should compile (no syntax errors). If you see *header* errors (missing semicolons, unknown types), fix them. If you see *link* errors (unresolved symbols), that's expected — Task 3 wires the cpp.

- [ ] **Step 3: Remove the temporary include**

Revert the test file change. The header is verified.

- [ ] **Step 4: Commit**

```powershell
git add src/cli/project_tab_ops.hpp
git commit -m "feat(cli): declare project-tab ops API + exception classes

New header for the project info/profile/aux verb family. Declares
InfoSetParams/ProfileSetParams, the AuxFolder enum, view structs for
show, the five new exception classes (BadCoverImage / BadAuxFile /
InvalidField / AuxNameError / AuxCollisionError), and helpers
(embed_cover_image / clear_cover_image / sanitize_aux_name / is_png).
Implementation lands in following commits.

Spec: docs/superpowers/specs/2026-05-20-orca-cli-project-tab-design.md
"
```

---

## Task 2: Stub `project_tab_ops.cpp` so the header has unresolved-but-declared symbols

**Files:**
- Create: `src/cli/project_tab_ops.cpp`

This task adds an empty implementation file so Task 3 can wire it into CMake. Real bodies arrive in Task 5+.

- [ ] **Step 1: Create the cpp with stubs that throw "not implemented"**

```cpp
// src/cli/project_tab_ops.cpp
#include "project_tab_ops.hpp"
#include <stdexcept>

namespace orca_cli {

const char* folder_flag(AuxFolder)       { throw std::logic_error("not implemented"); }
const char* folder_json_key(AuxFolder)   { throw std::logic_error("not implemented"); }
const char* folder_subdir(AuxFolder)     { throw std::logic_error("not implemented"); }

InfoView info_view(const ProjectState&)                              { throw std::logic_error("not implemented"); }
bool any_field_set(const InfoSetParams&)                             { throw std::logic_error("not implemented"); }
void info_set(ProjectState&, const InfoSetParams&)                   { throw std::logic_error("not implemented"); }
const std::vector<std::string>& allowed_info_fields()                { throw std::logic_error("not implemented"); }
void info_clear(ProjectState&, const std::vector<std::string>&)      { throw std::logic_error("not implemented"); }

ProfileView profile_view(const ProjectState&)                        { throw std::logic_error("not implemented"); }
bool any_field_set(const ProfileSetParams&)                          { throw std::logic_error("not implemented"); }
void profile_set(ProjectState&, const ProfileSetParams&)             { throw std::logic_error("not implemented"); }
const std::vector<std::string>& allowed_profile_fields()             { throw std::logic_error("not implemented"); }
void profile_clear(ProjectState&, const std::vector<std::string>&)   { throw std::logic_error("not implemented"); }

std::vector<AuxEntry> aux_list(const ProjectState&)                  { throw std::logic_error("not implemented"); }
void aux_add(ProjectState&, const AuxAddParams&)                     { throw std::logic_error("not implemented"); }
void aux_remove(ProjectState&, AuxFolder, const std::string&)        { throw std::logic_error("not implemented"); }
void aux_export(const ProjectState&, AuxFolder, const std::string&,
                const boost::filesystem::path&)                      { throw std::logic_error("not implemented"); }

bool is_png(const boost::filesystem::path&)                          { throw std::logic_error("not implemented"); }
void embed_cover_image(ProjectState&, const boost::filesystem::path&,
                       CoverTarget)                                  { throw std::logic_error("not implemented"); }
void clear_cover_image(ProjectState&, CoverTarget)                   { throw std::logic_error("not implemented"); }

std::string sanitize_aux_name(const std::string&)                    { throw std::logic_error("not implemented"); }

} // namespace orca_cli
```

- [ ] **Step 2: Commit (build verification happens in Task 3 after CMake wiring)**

```powershell
git add src/cli/project_tab_ops.cpp
git commit -m "feat(cli): stub project_tab_ops.cpp (throws not-implemented)

Empty bodies for every declared symbol; wired into CMake in the next
commit. Real implementations land per-op in following commits.
"
```

---

## Task 3: Wire `project_tab_ops.cpp` into `orca_cli_core` and confirm the link is clean

**Files:**
- Modify: `src/cli/CMakeLists.txt`

- [ ] **Step 1: Read the current CMakeLists.txt to find where `project_ops.cpp` is listed**

```powershell
Get-Content src/cli/CMakeLists.txt
```

Expected: you'll see an `add_library(orca_cli_core STATIC …)` (or similar) listing each cpp. `project_ops.cpp` and `placement.cpp` are good landmarks.

- [ ] **Step 2: Add `project_tab_ops.cpp` to the sources list, alphabetically adjacent to `project_ops.cpp`**

The exact edit depends on the existing layout. Example transformation:

Before (illustrative):
```cmake
add_library(orca_cli_core STATIC
    globals.cpp
    invariants.cpp
    io.cpp
    output.cpp
    placement.cpp
    project_ops.cpp
    ...
)
```

After:
```cmake
add_library(orca_cli_core STATIC
    globals.cpp
    invariants.cpp
    io.cpp
    output.cpp
    placement.cpp
    project_ops.cpp
    project_tab_ops.cpp
    ...
)
```

- [ ] **Step 3: Build to verify the link is clean**

```powershell
cmake --build build --config Release --target orca_cli_core -- -m
```

Expected: `orca_cli_core.lib` builds cleanly. No symbol redefinitions; no unresolved externals (the stubs satisfy every declaration).

- [ ] **Step 4: Build the test binary too**

```powershell
cmake --build build --config Release --target cli_tests -- -m
```

Expected: `cli_tests.exe` builds. Running it should still pass the baseline (Task 0 count); no new tests yet.

- [ ] **Step 5: Commit**

```powershell
git add src/cli/CMakeLists.txt
git commit -m "build(cli): link project_tab_ops.cpp into orca_cli_core"
```

---

## Task 4: Implement folder mapping helpers + AuxFolder enum metadata + tests

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_ops.cpp` (or, if you prefer cohesion, create `tests/cli/unit/test_project_tab_ops.cpp` now — see Task 5 for the file scaffold). For this small task it's fine to colocate with existing tests; Task 5 splits them out.

Implements the three `folder_*` functions and one trivially-asserted unit test per branch (12 assertions across 3 functions × 4 enum values). The directory names match `src/slic3r/GUI/Auxiliary.cpp:1043-1046` exactly (`"Model Pictures"`, `"Bill of Materials"`, `"Assembly Guide"`, `"Others"`).

- [ ] **Step 1: Write the failing test**

Add to `tests/cli/unit/test_project_ops.cpp` (top of file is fine):

```cpp
#include "project_tab_ops.hpp"

TEST_CASE("orca-cli: folder_flag round-trips all four buckets",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE(std::string(orca_cli::folder_flag(orca_cli::AuxFolder::pictures))       == "pictures");
    REQUIRE(std::string(orca_cli::folder_flag(orca_cli::AuxFolder::bom))            == "bom");
    REQUIRE(std::string(orca_cli::folder_flag(orca_cli::AuxFolder::assembly_guide)) == "assembly-guide");
    REQUIRE(std::string(orca_cli::folder_flag(orca_cli::AuxFolder::others))         == "others");
}

TEST_CASE("orca-cli: folder_json_key uses underscores (spec § 2.3)",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE(std::string(orca_cli::folder_json_key(orca_cli::AuxFolder::pictures))       == "pictures");
    REQUIRE(std::string(orca_cli::folder_json_key(orca_cli::AuxFolder::bom))            == "bom");
    REQUIRE(std::string(orca_cli::folder_json_key(orca_cli::AuxFolder::assembly_guide)) == "assembly_guide");
    REQUIRE(std::string(orca_cli::folder_json_key(orca_cli::AuxFolder::others))         == "others");
}

TEST_CASE("orca-cli: folder_subdir matches GUI Auxiliary panel directory names",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE(std::string(orca_cli::folder_subdir(orca_cli::AuxFolder::pictures))       == "Model Pictures");
    REQUIRE(std::string(orca_cli::folder_subdir(orca_cli::AuxFolder::bom))            == "Bill of Materials");
    REQUIRE(std::string(orca_cli::folder_subdir(orca_cli::AuxFolder::assembly_guide)) == "Assembly Guide");
    REQUIRE(std::string(orca_cli::folder_subdir(orca_cli::AuxFolder::others))         == "Others");
}
```

- [ ] **Step 2: Run; verify the test fails by throwing "not implemented"**

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][project-tab][unit]" --warn NoAssertions
```

Expected: 3 tests fail with `std::logic_error: not implemented`.

- [ ] **Step 3: Replace the three stubs with real bodies**

In `src/cli/project_tab_ops.cpp`:

```cpp
const char* folder_flag(AuxFolder f) {
    switch (f) {
        case AuxFolder::pictures:       return "pictures";
        case AuxFolder::bom:            return "bom";
        case AuxFolder::assembly_guide: return "assembly-guide";
        case AuxFolder::others:         return "others";
    }
    throw std::logic_error("unreachable: AuxFolder out of range");
}

const char* folder_json_key(AuxFolder f) {
    switch (f) {
        case AuxFolder::pictures:       return "pictures";
        case AuxFolder::bom:            return "bom";
        case AuxFolder::assembly_guide: return "assembly_guide";
        case AuxFolder::others:         return "others";
    }
    throw std::logic_error("unreachable: AuxFolder out of range");
}

const char* folder_subdir(AuxFolder f) {
    switch (f) {
        case AuxFolder::pictures:       return "Model Pictures";
        case AuxFolder::bom:            return "Bill of Materials";
        case AuxFolder::assembly_guide: return "Assembly Guide";
        case AuxFolder::others:         return "Others";
    }
    throw std::logic_error("unreachable: AuxFolder out of range");
}
```

- [ ] **Step 4: Build and re-run the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][project-tab][unit]" --warn NoAssertions
```

Expected: 3 tests pass.

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "feat(cli): folder enum string mappings for project aux verbs

Implements folder_flag (hyphen, CLI surface), folder_json_key
(underscore, JSON surface), and folder_subdir (matches the GUI
Auxiliary panel dir names at src/slic3r/GUI/Auxiliary.cpp:1043-1046).
+3 unit cases.

Spec § 2.1, § 2.3.
"
```

---

## Task 5: Implement `info_view` (read path for `project info show`) + tests

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Create: `tests/cli/unit/test_project_tab_ops.cpp` (new file for cohesion; subsequent tasks add to it)
- Modify: `tests/cli/CMakeLists.txt`

The unit-test file moves out of `test_project_ops.cpp` here; the three folder tests from Task 4 stay where they are (it's not worth a refactor).

- [ ] **Step 1: Create `tests/cli/unit/test_project_tab_ops.cpp` scaffold**

```cpp
// tests/cli/unit/test_project_tab_ops.cpp
#include <catch2/catch_all.hpp>
#include "project_tab_ops.hpp"
#include "project_ops.hpp"
#include "io.hpp"
#include "../test_common.hpp"

#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <boost/filesystem.hpp>

#include <memory>
#include <string>

using namespace orca_cli;

// Helper: construct an empty ProjectState with a single empty plate and
// nothing in model.model_info / model.profile_info. Mirrors the pattern in
// tests/cli/unit/test_project_ops.cpp for plate tests.
static ProjectState make_empty_state() {
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    return s;
}
```

- [ ] **Step 2: Wire the new test file into CMake**

In `tests/cli/CMakeLists.txt`, find the section that adds `unit/test_project_ops.cpp` to the `cli_tests` target and add `unit/test_project_tab_ops.cpp` alphabetically adjacent.

- [ ] **Step 3: Write the failing test (info_view empty + populated)**

Append to `tests/cli/unit/test_project_tab_ops.cpp`:

```cpp
TEST_CASE("orca-cli: info_view on empty model returns six empty strings",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    auto v = info_view(s);
    REQUIRE(v.title.empty());
    REQUIRE(v.description.empty());
    REQUIRE(v.license.empty());
    REQUIRE(v.copyright.empty());
    REQUIRE(v.cover.empty());
    REQUIRE(v.origin.empty());
}

TEST_CASE("orca-cli: info_view returns populated ModelInfo fields verbatim",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    s.model->model_info = std::make_shared<Slic3r::ModelInfo>();
    s.model->model_info->model_name  = "T";
    s.model->model_info->description = "D";
    s.model->model_info->license     = "MIT";
    s.model->model_info->copyright   = R"([{"author":"Z"}])";
    s.model->model_info->cover_file  = "Auxiliaries/.thumbnails/thumbnail_3mf.png";
    s.model->model_info->origin      = "OrcaSlicer";
    auto v = info_view(s);
    REQUIRE(v.title       == "T");
    REQUIRE(v.description == "D");
    REQUIRE(v.license     == "MIT");
    REQUIRE(v.copyright   == R"([{"author":"Z"}])");
    REQUIRE(v.cover       == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    REQUIRE(v.origin      == "OrcaSlicer");
}
```

- [ ] **Step 4: Run; verify tests fail**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][project-tab][unit]" --warn NoAssertions
```

Expected: the two new tests fail with `std::logic_error: not implemented` (the previous folder tests still pass).

- [ ] **Step 5: Implement `info_view` in `src/cli/project_tab_ops.cpp`**

Replace the stub:

```cpp
InfoView info_view(const ProjectState& s) {
    InfoView v;
    if (!s.model || !s.model->model_info) return v;  // all six fields empty
    const auto& mi = *s.model->model_info;
    v.title       = mi.model_name;
    v.description = mi.description;
    v.license     = mi.license;
    v.copyright   = mi.copyright;
    v.cover       = mi.cover_file;
    v.origin      = mi.origin;
    return v;
}
```

- [ ] **Step 6: Re-run; verify tests pass**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][project-tab][unit]" --warn NoAssertions
```

Expected: pass.

- [ ] **Step 7: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp tests/cli/CMakeLists.txt
git commit -m "feat(cli): info_view read path for project info show

+2 unit cases (empty model; populated model). Empty model returns six
empty strings (spec § 2.2 stable shape rule); populated reads each
ModelInfo field verbatim.
"
```

---

## Task 6: Implement `info_set` string fields (no cover yet) + tests

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

Cover handling lands in Task 9. This task only handles the four string fields and `any_field_set`. The `model.model_info == nullptr` allocation path is exercised explicitly per spec § 4.

- [ ] **Step 1: Write the failing tests**

Append to `tests/cli/unit/test_project_tab_ops.cpp`:

```cpp
TEST_CASE("orca-cli: any_field_set(InfoSetParams) detects every field individually",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE(!any_field_set(InfoSetParams{}));
    InfoSetParams p;
    p.title = "x";       REQUIRE(any_field_set(p));
    p = {}; p.description = "x"; REQUIRE(any_field_set(p));
    p = {}; p.license     = "x"; REQUIRE(any_field_set(p));
    p = {}; p.copyright   = "x"; REQUIRE(any_field_set(p));
    p = {}; p.cover       = boost::filesystem::path("x.png"); REQUIRE(any_field_set(p));
}

TEST_CASE("orca-cli: info_set allocates model_info when nullptr",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE(s.model->model_info == nullptr);
    InfoSetParams p; p.title = "Smoke";
    info_set(s, p);
    REQUIRE(s.model->model_info != nullptr);
    REQUIRE(s.model->model_info->model_name == "Smoke");
}

TEST_CASE("orca-cli: info_set batches multiple fields in one call",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    InfoSetParams p;
    p.title       = "T";
    p.description = "D";
    p.license     = "MIT";
    p.copyright   = "(c) 2026";
    info_set(s, p);
    REQUIRE(s.model->model_info->model_name  == "T");
    REQUIRE(s.model->model_info->description == "D");
    REQUIRE(s.model->model_info->license     == "MIT");
    REQUIRE(s.model->model_info->copyright   == "(c) 2026");
}

TEST_CASE("orca-cli: info_set is idempotent (re-set same value is fine)",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    InfoSetParams p; p.title = "T";
    info_set(s, p);
    info_set(s, p);
    REQUIRE(s.model->model_info->model_name == "T");
}
```

- [ ] **Step 2: Run; verify they fail**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][project-tab][unit]" --warn NoAssertions
```

- [ ] **Step 3: Implement `any_field_set(InfoSetParams)` and `info_set` (string-fields branch only)**

In `src/cli/project_tab_ops.cpp`:

```cpp
bool any_field_set(const InfoSetParams& p) {
    return p.title.has_value()
        || p.description.has_value()
        || p.license.has_value()
        || p.copyright.has_value()
        || p.cover.has_value();
}

void info_set(ProjectState& s, const InfoSetParams& p) {
    if (!s.model->model_info)
        s.model->model_info = std::make_shared<Slic3r::ModelInfo>();
    auto& mi = *s.model->model_info;
    if (p.title)       mi.model_name  = *p.title;
    if (p.description) mi.description = *p.description;
    if (p.license)     mi.license     = *p.license;
    if (p.copyright)   mi.copyright   = *p.copyright;
    if (p.cover)       embed_cover_image(s, *p.cover, CoverTarget::Info);
}
```

Note: `embed_cover_image` is still the not-implemented stub. The four new tests above don't set `p.cover`, so they don't exercise that path. A cover-specific test lands in Task 9.

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): info_set string fields (title/description/license/copyright)

+4 unit cases. Allocates model_info on first write, batches multiple
field flags in a single call, idempotent. --cover branch delegates
to embed_cover_image (still stubbed; lands in next commits)."
```

---

## Task 7: Implement `info_clear` (string fields only, no cover) + tests

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

Cover-clear (refcount) lands in Task 10. This task covers field-name validation, single-field clear, multi-field clear, and idempotency on an already-empty field.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("orca-cli: allowed_info_fields lists exactly the five legal names",
          "[orca-cli][project-tab][unit]")
{
    const auto& names = allowed_info_fields();
    REQUIRE(names.size() == 5);
    REQUIRE(std::count(names.begin(), names.end(), "title")       == 1);
    REQUIRE(std::count(names.begin(), names.end(), "description") == 1);
    REQUIRE(std::count(names.begin(), names.end(), "license")     == 1);
    REQUIRE(std::count(names.begin(), names.end(), "copyright")   == 1);
    REQUIRE(std::count(names.begin(), names.end(), "cover")       == 1);
}

TEST_CASE("orca-cli: info_clear nulls a single named string field",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    InfoSetParams p; p.title = "T"; p.description = "D";
    info_set(s, p);
    info_clear(s, {"title"});
    REQUIRE(s.model->model_info->model_name.empty());
    REQUIRE(s.model->model_info->description == "D");  // untouched
}

TEST_CASE("orca-cli: info_clear nulls multiple fields in one call",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    InfoSetParams p;
    p.title = "T"; p.description = "D"; p.license = "MIT"; p.copyright = "C";
    info_set(s, p);
    info_clear(s, {"title", "license"});
    REQUIRE(s.model->model_info->model_name.empty());
    REQUIRE(s.model->model_info->license.empty());
    REQUIRE(s.model->model_info->description == "D");
    REQUIRE(s.model->model_info->copyright   == "C");
}

TEST_CASE("orca-cli: info_clear rejects unknown field with InvalidField",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE_THROWS_AS(info_clear(s, {"profile_title"}), InvalidField);
    REQUIRE_THROWS_AS(info_clear(s, {"title", "bogus"}), InvalidField);
}

TEST_CASE("orca-cli: info_clear is idempotent on an already-empty field",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE_NOTHROW(info_clear(s, {"title"}));  // model_info still nullptr
    InfoSetParams p; p.title = "X"; info_set(s, p);
    REQUIRE_NOTHROW(info_clear(s, {"title"}));
    REQUIRE_NOTHROW(info_clear(s, {"title"}));  // double-clear no-op
    REQUIRE(s.model->model_info->model_name.empty());
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement `allowed_info_fields` and `info_clear` (string fields + cover dispatch stub)**

```cpp
const std::vector<std::string>& allowed_info_fields() {
    static const std::vector<std::string> v{
        "title", "description", "license", "copyright", "cover"
    };
    return v;
}

void info_clear(ProjectState& s, const std::vector<std::string>& fields) {
    const auto& allowed = allowed_info_fields();
    for (const auto& f : fields) {
        if (std::find(allowed.begin(), allowed.end(), f) == allowed.end()) {
            std::string msg = "unknown field: '" + f + "'. Allowed:";
            for (const auto& a : allowed) msg += " " + a;
            throw InvalidField(msg);
        }
    }
    if (!s.model->model_info) {
        // Nothing to clear on string fields. cover-clear may still need to
        // run if the canonical embedded image exists (handled by
        // clear_cover_image, which is a no-op on a missing file).
        for (const auto& f : fields)
            if (f == "cover") clear_cover_image(s, CoverTarget::Info);
        return;
    }
    auto& mi = *s.model->model_info;
    for (const auto& f : fields) {
        if      (f == "title")       mi.model_name.clear();
        else if (f == "description") mi.description.clear();
        else if (f == "license")     mi.license.clear();
        else if (f == "copyright")   mi.copyright.clear();
        else if (f == "cover")       clear_cover_image(s, CoverTarget::Info);
    }
}
```

Don't forget `#include <algorithm>` at the top of the cpp if it isn't already pulled in transitively.

- [ ] **Step 4: Re-run; verify pass**

Note: `clear_cover_image` is still a stub. The cover-clear branch in the cpp is unreachable from these tests (no test clears `"cover"`). It will be exercised by Task 10.

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): info_clear with field whitelist (InvalidField + idempotent)

+5 unit cases. allowed_info_fields exposes the five legal names;
info_clear nulls each, throws InvalidField on unknown name (lists
legal names in the message), idempotent on already-empty. --field
cover branch dispatches to clear_cover_image (stubbed)."
```

---

## Task 8: Implement `is_png` magic-byte check + tests

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

This is the foundation for `embed_cover_image` (Task 9). The PNG signature is 8 bytes: `89 50 4E 47 0D 0A 1A 0A`. JPG (`FF D8 FF`) must return false.

- [ ] **Step 1: Write the failing tests**

```cpp
#include <fstream>

TEST_CASE("orca-cli: is_png accepts valid 8-byte PNG signature",
          "[orca-cli][project-tab][unit]")
{
    auto p = orca_cli_test::make_temp_dir() / "valid.png";
    {
        std::ofstream f(p.string(), std::ios::binary);
        const unsigned char sig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
        f.write(reinterpret_cast<const char*>(sig), 8);
    }
    REQUIRE(is_png(p));
}

TEST_CASE("orca-cli: is_png rejects JPG signature",
          "[orca-cli][project-tab][unit]")
{
    auto p = orca_cli_test::make_temp_dir() / "actually.jpg";
    {
        std::ofstream f(p.string(), std::ios::binary);
        const unsigned char sig[8] = {0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46};
        f.write(reinterpret_cast<const char*>(sig), 8);
    }
    REQUIRE_FALSE(is_png(p));
}

TEST_CASE("orca-cli: is_png rejects truncated files (<8 bytes)",
          "[orca-cli][project-tab][unit]")
{
    auto p = orca_cli_test::make_temp_dir() / "tiny.png";
    {
        std::ofstream f(p.string(), std::ios::binary);
        const unsigned char sig[4] = {0x89,0x50,0x4E,0x47};  // only 4 of 8
        f.write(reinterpret_cast<const char*>(sig), 4);
    }
    REQUIRE_FALSE(is_png(p));
}

TEST_CASE("orca-cli: is_png returns false on missing file (no throw)",
          "[orca-cli][project-tab][unit]")
{
    auto p = orca_cli_test::make_temp_dir() / "does_not_exist.png";
    REQUIRE_FALSE(is_png(p));
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement `is_png`**

```cpp
bool is_png(const boost::filesystem::path& p) {
    boost::system::error_code ec;
    if (!boost::filesystem::is_regular_file(p, ec)) return false;
    std::ifstream f(p.string(), std::ios::binary);
    if (!f) return false;
    unsigned char buf[8] = {};
    f.read(reinterpret_cast<char*>(buf), 8);
    if (f.gcount() != 8) return false;
    static constexpr unsigned char kPngSig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    for (int i = 0; i < 8; ++i)
        if (buf[i] != kPngSig[i]) return false;
    return true;
}
```

Add `#include <fstream>` and `#include <boost/system/error_code.hpp>` at the top of the cpp.

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): is_png signature check (8-byte exact match)

+4 unit cases (PNG accept, JPG reject, truncated reject, missing
file returns false without throw). Foundation for embed_cover_image."
```

---

## Task 9: Implement `embed_cover_image` (PNG-only, dual-target) + tests

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

The function:
1. Validates source via `is_png` (else throws `BadCoverImage`).
2. Ensures `<auxiliary_temp_dir>/.thumbnails/` exists.
3. Copies bytes to `<auxiliary_temp_dir>/.thumbnails/thumbnail_3mf.png` (overwriting any prior copy).
4. Allocates `model_info` (or `profile_info`) if nullptr.
5. Sets the appropriate pointer to `"Auxiliaries/.thumbnails/thumbnail_3mf.png"` (no leading slash, per spec § 3.4).

Important detail: `Model::get_auxiliary_file_temp_path()` returns the path on first call and creates it. Use that and DO NOT assume it pre-exists.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
// Test helper: write a valid 1×1 transparent PNG to `p`. Returns the bytes
// written so tests can byte-compare after roundtrip.
inline std::vector<unsigned char> write_tiny_png(const boost::filesystem::path& p) {
    // Minimal valid 1x1 transparent PNG (handcrafted, ~67 bytes). Source:
    // <https://en.wikipedia.org/wiki/Portable_Network_Graphics#File_format>.
    static const unsigned char kPng[] = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,
        0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
        0x08,0x06,0x00,0x00,0x00,0x1F,0x15,0xC4,
        0x89,0x00,0x00,0x00,0x0D,0x49,0x44,0x41,
        0x54,0x78,0x9C,0x63,0x00,0x01,0x00,0x00,
        0x05,0x00,0x01,0x0D,0x0A,0x2D,0xB4,0x00,
        0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,
        0x42,0x60,0x82,
    };
    std::ofstream f(p.string(), std::ios::binary);
    f.write(reinterpret_cast<const char*>(kPng), sizeof(kPng));
    return std::vector<unsigned char>(kPng, kPng + sizeof(kPng));
}

inline std::vector<unsigned char> read_all(const boost::filesystem::path& p) {
    std::ifstream f(p.string(), std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),
                                      std::istreambuf_iterator<char>{});
}
} // namespace

TEST_CASE("orca-cli: embed_cover_image accepts PNG and points info cover_file at canonical path",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "hero.png";
    auto bytes = write_tiny_png(src);

    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Info);

    REQUIRE(s.model->model_info != nullptr);
    REQUIRE(s.model->model_info->cover_file
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");

    auto aux = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto landed = aux / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(boost::filesystem::exists(landed));
    REQUIRE(read_all(landed) == bytes);
}

TEST_CASE("orca-cli: embed_cover_image profile target sets ProfileCover and reuses canonical path",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "hero.png";
    write_tiny_png(src);

    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Profile);

    REQUIRE(s.model->profile_info != nullptr);
    REQUIRE(s.model->profile_info->ProfileCover
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
}

TEST_CASE("orca-cli: embed_cover_image second call overwrites canonical file bytes",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto a = tmp / "a.png"; auto bytes_a = write_tiny_png(a);
    auto b = tmp / "b.png";
    // Make b a distinct PNG: copy a's bytes, then bump one byte in the IDAT
    // payload zone (bytes 41-53 are the deflated image data; index 50 is
    // a safe tweak that keeps the file size constant — we don't decode it).
    {
        auto bytes_b_src = bytes_a;
        bytes_b_src[50] ^= 0x7F;
        std::ofstream f(b.string(), std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes_b_src.data()),
                bytes_b_src.size());
    }
    auto bytes_b = read_all(b);
    REQUIRE(bytes_a != bytes_b);   // pre-condition guard

    auto s = make_empty_state();
    embed_cover_image(s, a, CoverTarget::Info);
    embed_cover_image(s, b, CoverTarget::Profile);  // overwrites a's bytes

    auto aux = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto landed = aux / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(read_all(landed) == bytes_b);
    REQUIRE(s.model->model_info->cover_file
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    REQUIRE(s.model->profile_info->ProfileCover
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
}

TEST_CASE("orca-cli: embed_cover_image rejects JPG with BadCoverImage",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto jpg = tmp / "fake.png";  // .png extension, JPG bytes
    {
        std::ofstream f(jpg.string(), std::ios::binary);
        const unsigned char sig[16] = {
            0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46,
            0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,
        };
        f.write(reinterpret_cast<const char*>(sig), 16);
    }
    auto s = make_empty_state();
    REQUIRE_THROWS_AS(embed_cover_image(s, jpg, CoverTarget::Info), BadCoverImage);
    REQUIRE(s.model->model_info == nullptr);  // not allocated on failure
}

TEST_CASE("orca-cli: embed_cover_image rejects missing source with BadCoverImage",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto missing = tmp / "nope.png";
    auto s = make_empty_state();
    REQUIRE_THROWS_AS(embed_cover_image(s, missing, CoverTarget::Info), BadCoverImage);
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement `embed_cover_image`**

```cpp
namespace {
constexpr const char* kCoverPath = "Auxiliaries/.thumbnails/thumbnail_3mf.png";
constexpr const char* kCoverSubdir = ".thumbnails";
constexpr const char* kCoverName   = "thumbnail_3mf.png";
} // namespace

void embed_cover_image(ProjectState&                  s,
                       const boost::filesystem::path& src,
                       CoverTarget                    target)
{
    if (!is_png(src)) {
        // Surface a useful diagnostic. Read up to 8 bytes for the hex prefix.
        std::string prefix = "<unreadable>";
        std::ifstream f(src.string(), std::ios::binary);
        if (f) {
            unsigned char buf[8] = {};
            f.read(reinterpret_cast<char*>(buf), 8);
            auto n = static_cast<std::size_t>(f.gcount());
            std::ostringstream os; os << std::hex << std::uppercase
                                      << std::setfill('0');
            for (std::size_t i = 0; i < n; ++i)
                os << std::setw(2) << static_cast<int>(buf[i])
                   << (i + 1 == n ? "" : " ");
            prefix = os.str();
        }
        throw BadCoverImage("not a valid PNG (expected signature 89 50 4E 47 "
                            "0D 0A 1A 0A; got [" + prefix + "]) at: "
                            + src.string());
    }

    auto aux = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto subdir = aux / kCoverSubdir;
    boost::system::error_code ec;
    boost::filesystem::create_directories(subdir, ec);
    if (ec) throw BadCoverImage("failed to prepare cover dir " + subdir.string()
                                + ": " + ec.message());

    auto landed = subdir / kCoverName;
    boost::filesystem::copy_file(src, landed,
        boost::filesystem::copy_options::overwrite_existing, ec);
    if (ec) throw BadCoverImage("failed to copy cover image: " + ec.message());

    if (target == CoverTarget::Info) {
        if (!s.model->model_info)
            s.model->model_info = std::make_shared<Slic3r::ModelInfo>();
        s.model->model_info->cover_file = kCoverPath;
    } else {
        if (!s.model->profile_info)
            s.model->profile_info = std::make_shared<Slic3r::ModelProfileInfo>();
        s.model->profile_info->ProfileCover = kCoverPath;
    }
}
```

Add `#include <iomanip>`, `#include <sstream>`, and ensure `<boost/filesystem.hpp>` is included.

Note: the "not allocated on failure" assertion in test #4 above requires that we throw BEFORE the allocation. The implementation above satisfies this — `is_png` is the first call.

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Wire info_set's --cover branch (already in place from Task 6, but now non-stub)**

`info_set` already calls `embed_cover_image`. Add one test that goes through info_set:

```cpp
TEST_CASE("orca-cli: info_set --cover routes through embed_cover_image",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "via.png"; write_tiny_png(src);
    auto s = make_empty_state();
    InfoSetParams p; p.cover = src;
    info_set(s, p);
    REQUIRE(s.model->model_info->cover_file
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
}
```

- [ ] **Step 6: Build and pass**

- [ ] **Step 7: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): embed_cover_image (PNG-only, dual-target)

+6 unit cases. Magic-byte gate (BadCoverImage on non-PNG / missing
source — hex prefix surfaced in error message). Copies bytes to
canonical Auxiliaries/.thumbnails/thumbnail_3mf.png; sets cover_file
on info OR profile (shared canonical path overwrites the other
surface's bytes). info_set --cover branch now live."
```

---

## Task 10: Implement `clear_cover_image` refcount + tests (four cases)

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

The four cases (spec § 3.5):
1. Only Info pointer set → clear Info → file deleted; Info pointer null.
2. Only Profile pointer set → clear Profile → file deleted; Profile pointer null.
3. Both pointers set → clear Info → file **kept**; Info pointer null, Profile pointer intact.
4. Both pointers set → clear Info then clear Profile → file deleted on the second clear; both pointers null.

- [ ] **Step 1: Write the four tests + an idempotency test**

```cpp
TEST_CASE("orca-cli: clear_cover_image — only-info clear deletes the file",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "c.png"; write_tiny_png(src);
    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Info);
    auto landed = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
                  / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(boost::filesystem::exists(landed));

    clear_cover_image(s, CoverTarget::Info);
    REQUIRE(s.model->model_info->cover_file.empty());
    REQUIRE_FALSE(boost::filesystem::exists(landed));
}

TEST_CASE("orca-cli: clear_cover_image — only-profile clear deletes the file",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "c.png"; write_tiny_png(src);
    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Profile);
    auto landed = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
                  / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(boost::filesystem::exists(landed));

    clear_cover_image(s, CoverTarget::Profile);
    REQUIRE(s.model->profile_info->ProfileCover.empty());
    REQUIRE_FALSE(boost::filesystem::exists(landed));
}

TEST_CASE("orca-cli: clear_cover_image — both-set clear-info keeps file (profile still owns)",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "c.png"; write_tiny_png(src);
    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Info);
    embed_cover_image(s, src, CoverTarget::Profile);
    auto landed = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
                  / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(boost::filesystem::exists(landed));

    clear_cover_image(s, CoverTarget::Info);
    REQUIRE(s.model->model_info->cover_file.empty());
    REQUIRE(s.model->profile_info->ProfileCover
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    REQUIRE(boost::filesystem::exists(landed));  // kept!
}

TEST_CASE("orca-cli: clear_cover_image — both-set sequential clears delete on second",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "c.png"; write_tiny_png(src);
    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Info);
    embed_cover_image(s, src, CoverTarget::Profile);
    auto landed = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
                  / ".thumbnails" / "thumbnail_3mf.png";

    clear_cover_image(s, CoverTarget::Info);
    REQUIRE(boost::filesystem::exists(landed));  // profile still owns

    clear_cover_image(s, CoverTarget::Profile);
    REQUIRE(s.model->profile_info->ProfileCover.empty());
    REQUIRE_FALSE(boost::filesystem::exists(landed));
}

TEST_CASE("orca-cli: clear_cover_image is idempotent (no-op on already-empty)",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE_NOTHROW(clear_cover_image(s, CoverTarget::Info));
    REQUIRE_NOTHROW(clear_cover_image(s, CoverTarget::Profile));
    // info_clear --field cover when model_info is nullptr also OK:
    REQUIRE_NOTHROW(info_clear(s, {"cover"}));
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement `clear_cover_image`**

```cpp
namespace {
bool info_cover_empty(const ProjectState& s) {
    return !s.model->model_info || s.model->model_info->cover_file.empty();
}
bool profile_cover_empty(const ProjectState& s) {
    return !s.model->profile_info || s.model->profile_info->ProfileCover.empty();
}
} // namespace

void clear_cover_image(ProjectState& s, CoverTarget target) {
    if (target == CoverTarget::Info) {
        if (s.model->model_info) s.model->model_info->cover_file.clear();
    } else {
        if (s.model->profile_info) s.model->profile_info->ProfileCover.clear();
    }

    // Refcount: only delete the canonical file when BOTH surfaces are empty.
    if (!info_cover_empty(s) || !profile_cover_empty(s)) return;

    auto aux = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto landed = aux / kCoverSubdir / kCoverName;
    boost::system::error_code ec;
    boost::filesystem::remove(landed, ec);  // best-effort; absent file -> no-op
}
```

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): clear_cover_image refcount (spec § 3.5)

+5 unit cases covering all four refcount branches plus idempotency.
File at Auxiliaries/.thumbnails/thumbnail_3mf.png is deleted only
when BOTH info and profile pointers are empty after the clear.
info_clear --field cover branch now live."
```

---

## Task 11: Implement `profile_view`, `profile_set`, `profile_clear` + tests

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

Symmetric to info but on `ModelProfileInfo`. Fields: title (`ProfileTile`, sic), description (`ProfileDescription`), cover (`ProfileCover`). Read-only via view: user_id (`ProfileUserId`), user_name (`ProfileUserName`).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("orca-cli: profile_view on empty model returns five empty strings",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    auto v = profile_view(s);
    REQUIRE(v.title.empty());
    REQUIRE(v.description.empty());
    REQUIRE(v.cover.empty());
    REQUIRE(v.user_id.empty());
    REQUIRE(v.user_name.empty());
}

TEST_CASE("orca-cli: profile_view surfaces user_id and user_name (read-only)",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    s.model->profile_info = std::make_shared<Slic3r::ModelProfileInfo>();
    s.model->profile_info->ProfileTile        = "T";
    s.model->profile_info->ProfileDescription = "D";
    s.model->profile_info->ProfileCover       = "Auxiliaries/.thumbnails/thumbnail_3mf.png";
    s.model->profile_info->ProfileUserId      = "id-42";
    s.model->profile_info->ProfileUserName    = "alice";
    auto v = profile_view(s);
    REQUIRE(v.title       == "T");
    REQUIRE(v.description == "D");
    REQUIRE(v.cover       == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    REQUIRE(v.user_id     == "id-42");
    REQUIRE(v.user_name   == "alice");
}

TEST_CASE("orca-cli: profile_set allocates profile_info; batches fields",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE(s.model->profile_info == nullptr);
    ProfileSetParams p; p.title = "PT"; p.description = "PD";
    profile_set(s, p);
    REQUIRE(s.model->profile_info != nullptr);
    REQUIRE(s.model->profile_info->ProfileTile        == "PT");
    REQUIRE(s.model->profile_info->ProfileDescription == "PD");
}

TEST_CASE("orca-cli: allowed_profile_fields lists exactly three legal names",
          "[orca-cli][project-tab][unit]")
{
    const auto& n = allowed_profile_fields();
    REQUIRE(n.size() == 3);
    REQUIRE(std::count(n.begin(), n.end(), "title")       == 1);
    REQUIRE(std::count(n.begin(), n.end(), "description") == 1);
    REQUIRE(std::count(n.begin(), n.end(), "cover")       == 1);
}

TEST_CASE("orca-cli: profile_clear rejects info-only fields with InvalidField",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE_THROWS_AS(profile_clear(s, {"license"}),   InvalidField);
    REQUIRE_THROWS_AS(profile_clear(s, {"user_id"}),   InvalidField);
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement the three functions**

```cpp
ProfileView profile_view(const ProjectState& s) {
    ProfileView v;
    if (!s.model || !s.model->profile_info) return v;
    const auto& p = *s.model->profile_info;
    v.title       = p.ProfileTile;          // sic
    v.description = p.ProfileDescription;
    v.cover       = p.ProfileCover;
    v.user_id     = p.ProfileUserId;
    v.user_name   = p.ProfileUserName;
    return v;
}

bool any_field_set(const ProfileSetParams& p) {
    return p.title.has_value() || p.description.has_value() || p.cover.has_value();
}

void profile_set(ProjectState& s, const ProfileSetParams& p) {
    if (!s.model->profile_info)
        s.model->profile_info = std::make_shared<Slic3r::ModelProfileInfo>();
    auto& pi = *s.model->profile_info;
    if (p.title)       pi.ProfileTile        = *p.title;
    if (p.description) pi.ProfileDescription = *p.description;
    if (p.cover)       embed_cover_image(s, *p.cover, CoverTarget::Profile);
}

const std::vector<std::string>& allowed_profile_fields() {
    static const std::vector<std::string> v{"title", "description", "cover"};
    return v;
}

void profile_clear(ProjectState& s, const std::vector<std::string>& fields) {
    const auto& allowed = allowed_profile_fields();
    for (const auto& f : fields) {
        if (std::find(allowed.begin(), allowed.end(), f) == allowed.end()) {
            std::string msg = "unknown field: '" + f + "'. Allowed:";
            for (const auto& a : allowed) msg += " " + a;
            throw InvalidField(msg);
        }
    }
    if (!s.model->profile_info) {
        for (const auto& f : fields)
            if (f == "cover") clear_cover_image(s, CoverTarget::Profile);
        return;
    }
    auto& pi = *s.model->profile_info;
    for (const auto& f : fields) {
        if      (f == "title")       pi.ProfileTile.clear();
        else if (f == "description") pi.ProfileDescription.clear();
        else if (f == "cover")       clear_cover_image(s, CoverTarget::Profile);
    }
}
```

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): profile_view / profile_set / profile_clear

+5 unit cases. Symmetric to info ops; user_id and user_name exposed
read-only via view (no set/clear); cover routes through the same
embed_cover_image / clear_cover_image helpers as info (refcount
shared)."
```

---

## Task 12: Implement `sanitize_aux_name` + table-driven tests

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

Spec § 2.1 ruleset:
- empty
- contains `/`, `\`, or `\0`
- equals `.` or `..`
- leading or trailing whitespace
- leading or trailing `.`
- Windows reserved device names (case-insensitive, with or without extension): `CON`, `PRN`, `AUX`, `NUL`, `COM1`-`COM9`, `LPT1`-`LPT9`

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("orca-cli: sanitize_aux_name accepts legitimate filenames",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE(sanitize_aux_name("model.stl")              == "model.stl");
    REQUIRE(sanitize_aux_name("assembly_step_1.png")    == "assembly_step_1.png");
    REQUIRE(sanitize_aux_name("Bill of Materials.pdf")  == "Bill of Materials.pdf");
    REQUIRE(sanitize_aux_name("a.b.c.png")              == "a.b.c.png");
    REQUIRE(sanitize_aux_name("CON_NotReserved.txt")    == "CON_NotReserved.txt");
}

TEST_CASE("orca-cli: sanitize_aux_name rejects path-traversal and separators",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE_THROWS_AS(sanitize_aux_name(""),              AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("a/b"),           AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("a\\b"),          AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name(std::string("a\0b", 3)), AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("."),             AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name(".."),            AuxNameError);
}

TEST_CASE("orca-cli: sanitize_aux_name rejects leading/trailing dot or whitespace",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE_THROWS_AS(sanitize_aux_name(".hidden.png"),   AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("trail."),        AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name(" leading.png"),  AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("trailing.png "), AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("\ttab.png"),     AuxNameError);
}

TEST_CASE("orca-cli: sanitize_aux_name rejects Windows reserved names (case-insensitive, with extension)",
          "[orca-cli][project-tab][unit]")
{
    for (const std::string& n : {"CON", "con", "Con", "PRN", "AUX", "NUL",
                                 "COM1", "com9", "LPT1", "lpt9"}) {
        DYNAMIC_SECTION("bare: " << n) {
            REQUIRE_THROWS_AS(sanitize_aux_name(n), AuxNameError);
        }
        DYNAMIC_SECTION("with ext: " << n + ".png") {
            REQUIRE_THROWS_AS(sanitize_aux_name(n + ".png"), AuxNameError);
        }
    }
    // Boundary: COM10 / LPT10 are NOT reserved.
    REQUIRE(sanitize_aux_name("COM10.png") == "COM10.png");
    REQUIRE(sanitize_aux_name("LPT10.png") == "LPT10.png");
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement `sanitize_aux_name`**

```cpp
namespace {
bool is_reserved_windows_name(const std::string& base) {
    // base = portion before the first '.' (case-insensitive compare).
    auto upper = base; for (auto& c : upper) c = std::toupper(static_cast<unsigned char>(c));
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL")
        return true;
    if (upper.size() == 4 && (upper.substr(0,3) == "COM" || upper.substr(0,3) == "LPT")) {
        char d = upper[3];
        if (d >= '1' && d <= '9') return true;
    }
    return false;
}
} // namespace

std::string sanitize_aux_name(const std::string& name) {
    if (name.empty())
        throw AuxNameError("aux name must not be empty");
    for (char c : name) {
        if (c == '/' || c == '\\')
            throw AuxNameError("aux name must not contain path separators: '" + name + "'");
        if (c == '\0')
            throw AuxNameError("aux name must not contain null bytes");
    }
    if (name == "." || name == "..")
        throw AuxNameError("aux name must not be '.' or '..'");
    if (name.front() == '.' || name.back() == '.')
        throw AuxNameError("aux name must not start or end with '.': '" + name + "'");
    if (std::isspace(static_cast<unsigned char>(name.front())) ||
        std::isspace(static_cast<unsigned char>(name.back())))
        throw AuxNameError("aux name must not start or end with whitespace: '" + name + "'");
    auto dot = name.find('.');
    std::string base = (dot == std::string::npos) ? name : name.substr(0, dot);
    if (is_reserved_windows_name(base))
        throw AuxNameError("aux name uses a Windows reserved device name: '" + name + "'");
    return name;
}
```

Add `#include <cctype>` and `#include <algorithm>` if not already present.

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): sanitize_aux_name with Windows-reserved-name rejection

+4 unit cases. Rejects empty, path separators, null bytes, '.' / '..',
leading/trailing dot or whitespace, and CON/PRN/AUX/NUL/COM1-9/LPT1-9
(case-insensitive, with or without extension). Boundary: COM10/LPT10
accepted (not reserved). Spec § 2.1."
```

---

## Task 13: Implement `aux_list` (read path) + tests

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

Walks all four bucket subdirs under the model's auxiliary temp dir. The
plan assumes `model.get_auxiliary_file_temp_path()` returns the path to
a directory whose immediate children are the bucket subdirs ("Model
Pictures", "Bill of Materials", "Assembly Guide", "Others"). This must
be verified before writing the body.

- [ ] **Step 0: Verify `get_auxiliary_file_temp_path()`'s return-path shape (MANDATORY before Step 3)**

```powershell
# 1. Find the implementation.
Select-String -Path src/libslic3r/Model.cpp -Pattern "get_auxiliary_file_temp_path" -Context 0,8
# Expected: a method body that returns either:
#   (a) a string ending in ".../Auxiliaries"  -- children are the buckets directly
#   (b) a string ending in some other dir, with "/Auxiliaries" appended inside

# 2. Cross-reference how the GUI walks it (the canonical consumer).
Select-String -Path src/slic3r/GUI/Auxiliary.cpp -Pattern "get_auxiliary_file_temp_path|m_root_dir" -Context 0,4
# Expected: AuxiliaryPanel uses the returned path directly as m_root_dir
# and lists "Model Pictures" / "Bill of Materials" etc. as direct
# children of it.
```

If the path's children are the bucket subdirs (case a), the
implementation below is correct as-written: `aux_root / folder_subdir(f)`
resolves to the bucket. If there's an intermediate component (case b),
prepend it consistently in every aux op:

```cpp
auto aux_root = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
              / "Auxiliaries";   // <- only if Step 0 finds an intermediate
```

Apply the same correction in `embed_cover_image` (Task 9), `aux_add`
(Task 14), `aux_remove` (Task 15), `aux_export` (Task 16). The unit
tests will pass either way — they round-trip through the same helper —
but a wrong shape here means real 3mf round-trips silently land files
in the wrong place.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("orca-cli: aux_list returns empty four-bucket result on fresh project",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    auto entries = aux_list(s);
    REQUIRE(entries.empty());
}

TEST_CASE("orca-cli: aux_list walks every populated bucket and stamps size",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    auto aux_root = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto pics = aux_root / folder_subdir(AuxFolder::pictures);
    auto bom  = aux_root / folder_subdir(AuxFolder::bom);
    boost::filesystem::create_directories(pics);
    boost::filesystem::create_directories(bom);
    {
        std::ofstream(pics.string() + "/hero.png", std::ios::binary).write("HELLO", 5);
        std::ofstream(bom.string()  + "/parts.csv", std::ios::binary).write("a,b,c\n", 6);
    }

    auto entries = aux_list(s);
    REQUIRE(entries.size() == 2u);
    bool saw_hero = false, saw_parts = false;
    for (const auto& e : entries) {
        if (e.folder == AuxFolder::pictures && e.name == "hero.png") {
            REQUIRE(e.size == 5u); saw_hero = true;
        }
        if (e.folder == AuxFolder::bom && e.name == "parts.csv") {
            REQUIRE(e.size == 6u); saw_parts = true;
        }
    }
    REQUIRE(saw_hero);
    REQUIRE(saw_parts);
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement `aux_list`**

```cpp
std::vector<AuxEntry> aux_list(const ProjectState& s) {
    std::vector<AuxEntry> out;
    auto aux_root = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    boost::system::error_code ec;
    if (!boost::filesystem::is_directory(aux_root, ec)) return out;

    for (auto folder : {AuxFolder::pictures, AuxFolder::bom,
                        AuxFolder::assembly_guide, AuxFolder::others}) {
        auto sub = aux_root / folder_subdir(folder);
        if (!boost::filesystem::is_directory(sub, ec)) continue;
        for (auto it = boost::filesystem::directory_iterator(sub, ec);
             it != boost::filesystem::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!boost::filesystem::is_regular_file(it->path(), ec)) continue;
            AuxEntry e;
            e.folder = folder;
            e.name   = it->path().filename().string();
            e.size   = static_cast<std::uint64_t>(
                boost::filesystem::file_size(it->path(), ec));
            if (ec) e.size = 0;
            out.push_back(std::move(e));
        }
    }
    return out;
}
```

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): aux_list walks four auxiliary buckets

+2 unit cases (empty project; populated buckets with size). Read-only
walk of <aux_temp>/<bucket>/*; missing buckets silently skipped.
Caller stitches the four-key JSON shape per spec § 2.2."
```

---

## Task 14: Implement `aux_add` + tests (happy, collision, --force, --name override)

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("orca-cli: aux_add happy path copies source into bucket",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "hero.png"; write_tiny_png(src);

    auto s = make_empty_state();
    AuxAddParams p; p.folder = AuxFolder::pictures; p.file = src;
    aux_add(s, p);

    auto aux_root = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto landed = aux_root / folder_subdir(AuxFolder::pictures) / "hero.png";
    REQUIRE(boost::filesystem::exists(landed));
    REQUIRE(read_all(landed) == read_all(src));
}

TEST_CASE("orca-cli: aux_add --name overrides in-3mf basename",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "local.png"; write_tiny_png(src);

    auto s = make_empty_state();
    AuxAddParams p; p.folder = AuxFolder::pictures; p.file = src;
    p.name = std::string("renamed.png");
    aux_add(s, p);

    auto aux_root = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto landed = aux_root / folder_subdir(AuxFolder::pictures) / "renamed.png";
    REQUIRE(boost::filesystem::exists(landed));
    REQUIRE_FALSE(boost::filesystem::exists(aux_root / folder_subdir(AuxFolder::pictures) / "local.png"));
}

TEST_CASE("orca-cli: aux_add rejects missing source with BadAuxFile",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "does_not_exist.pdf";
    auto s = make_empty_state();
    AuxAddParams p; p.folder = AuxFolder::bom; p.file = src;
    REQUIRE_THROWS_AS(aux_add(s, p), BadAuxFile);
}

TEST_CASE("orca-cli: aux_add collision throws AuxCollisionError without --force",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "x.png"; write_tiny_png(src);

    auto s = make_empty_state();
    AuxAddParams p; p.folder = AuxFolder::pictures; p.file = src;
    aux_add(s, p);
    REQUIRE_THROWS_AS(aux_add(s, p), AuxCollisionError);
}

TEST_CASE("orca-cli: aux_add --force overwrites existing entry",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src1 = tmp / "v1.png"; write_tiny_png(src1);
    auto src2 = tmp / "v2.png";
    {
        // distinct bytes by re-writing the same PNG then padding with one
        // extra byte at end:
        auto bytes = read_all(src1);
        bytes.push_back(0xFF);
        std::ofstream(src2.string(), std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    auto s = make_empty_state();
    AuxAddParams p1; p1.folder = AuxFolder::others; p1.file = src1;
    p1.name = std::string("a.bin"); aux_add(s, p1);

    AuxAddParams p2; p2.folder = AuxFolder::others; p2.file = src2;
    p2.name = std::string("a.bin"); p2.force = true; aux_add(s, p2);

    auto landed = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
                  / folder_subdir(AuxFolder::others) / "a.bin";
    REQUIRE(read_all(landed) == read_all(src2));
}

TEST_CASE("orca-cli: aux_add propagates AuxNameError from sanitization",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "x.png"; write_tiny_png(src);
    auto s = make_empty_state();
    AuxAddParams p; p.folder = AuxFolder::pictures; p.file = src;
    p.name = std::string("CON.png");  // Windows-reserved
    REQUIRE_THROWS_AS(aux_add(s, p), AuxNameError);
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement `aux_add`**

```cpp
void aux_add(ProjectState& s, const AuxAddParams& p) {
    boost::system::error_code ec;
    if (!boost::filesystem::is_regular_file(p.file, ec))
        throw BadAuxFile("aux source file not found or not readable: " + p.file.string());

    std::string raw_name = p.name.value_or(p.file.filename().string());
    std::string clean = sanitize_aux_name(raw_name);  // throws AuxNameError

    auto aux_root = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto sub = aux_root / folder_subdir(p.folder);
    boost::filesystem::create_directories(sub, ec);
    if (ec) throw BadAuxFile("failed to prepare aux dir " + sub.string() + ": " + ec.message());

    auto dst = sub / clean;
    if (boost::filesystem::exists(dst, ec) && !p.force)
        throw AuxCollisionError("aux entry already exists: "
            + std::string(folder_flag(p.folder)) + "/" + clean
            + " (pass --force to overwrite)");

    boost::filesystem::copy_file(p.file, dst,
        boost::filesystem::copy_options::overwrite_existing, ec);
    if (ec) throw BadAuxFile("failed to copy aux file: " + ec.message());
}
```

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): aux_add with sanitization, collision, --force, --name

+6 unit cases. Source must exist (BadAuxFile); name is sanitized
(AuxNameError); existing target without --force is AuxCollisionError;
--force overwrites; --name overrides the in-3mf basename."
```

---

## Task 15: Implement `aux_remove` + tests

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("orca-cli: aux_remove deletes the named entry",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "x.png"; write_tiny_png(src);
    auto s = make_empty_state();
    AuxAddParams p; p.folder = AuxFolder::pictures; p.file = src;
    aux_add(s, p);

    aux_remove(s, AuxFolder::pictures, "x.png");
    auto landed = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
                  / folder_subdir(AuxFolder::pictures) / "x.png";
    REQUIRE_FALSE(boost::filesystem::exists(landed));
}

TEST_CASE("orca-cli: aux_remove throws out_of_range on missing entry",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE_THROWS_AS(aux_remove(s, AuxFolder::pictures, "missing.png"),
                      std::out_of_range);
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement `aux_remove`**

```cpp
void aux_remove(ProjectState& s, AuxFolder folder, const std::string& name) {
    boost::system::error_code ec;
    auto aux_root = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto path = aux_root / folder_subdir(folder) / name;
    if (!boost::filesystem::is_regular_file(path, ec))
        throw std::out_of_range("aux entry not found: "
            + std::string(folder_flag(folder)) + "/" + name);
    boost::filesystem::remove(path, ec);
    if (ec) throw std::runtime_error("failed to remove aux entry: " + ec.message());
}
```

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): aux_remove (happy + missing)

+2 unit cases. Missing entry throws std::out_of_range so the
MutationExceptionMap default maps it to unknown_reference (exit 6)
without any new exit code."
```

---

## Task 16: Implement `aux_export` + tests (file dest, dir dest, missing parent, missing entry, overwrite)

**Files:**
- Modify: `src/cli/project_tab_ops.cpp`
- Modify: `tests/cli/unit/test_project_tab_ops.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("orca-cli: aux_export writes to a file path destination",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "x.png"; auto src_bytes = read_all(src); write_tiny_png(src);
    src_bytes = read_all(src);
    auto s = make_empty_state();
    AuxAddParams p; p.folder = AuxFolder::pictures; p.file = src;
    aux_add(s, p);

    auto out_dir = orca_cli_test::make_temp_dir();
    auto dst = out_dir / "exported.png";
    aux_export(s, AuxFolder::pictures, "x.png", dst);
    REQUIRE(boost::filesystem::exists(dst));
    REQUIRE(read_all(dst) == src_bytes);
}

TEST_CASE("orca-cli: aux_export writes into an existing directory destination",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "x.png"; write_tiny_png(src);
    auto src_bytes = read_all(src);
    auto s = make_empty_state();
    AuxAddParams p; p.folder = AuxFolder::pictures; p.file = src;
    aux_add(s, p);

    auto out_dir = orca_cli_test::make_temp_dir();  // existing dir
    aux_export(s, AuxFolder::pictures, "x.png", out_dir);
    auto landed = out_dir / "x.png";
    REQUIRE(boost::filesystem::exists(landed));
    REQUIRE(read_all(landed) == src_bytes);
}

TEST_CASE("orca-cli: aux_export rejects non-existent --to parent dir",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "x.png"; write_tiny_png(src);
    auto s = make_empty_state();
    AuxAddParams p; p.folder = AuxFolder::pictures; p.file = src;
    aux_add(s, p);

    auto bad = orca_cli_test::make_temp_dir() / "does_not_exist_dir" / "out.png";
    REQUIRE_THROWS_AS(aux_export(s, AuxFolder::pictures, "x.png", bad),
                      std::invalid_argument);
}

TEST_CASE("orca-cli: aux_export missing entry throws std::out_of_range",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    auto dst = orca_cli_test::make_temp_dir() / "out.png";
    REQUIRE_THROWS_AS(aux_export(s, AuxFolder::pictures, "missing.png", dst),
                      std::out_of_range);
}

TEST_CASE("orca-cli: aux_export overwrites existing destination without prompting",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "x.png"; write_tiny_png(src);
    auto s = make_empty_state();
    AuxAddParams p; p.folder = AuxFolder::pictures; p.file = src;
    aux_add(s, p);

    auto dst = orca_cli_test::make_temp_dir() / "out.png";
    std::ofstream(dst.string(), std::ios::binary).write("OLD", 3);
    REQUIRE(boost::filesystem::exists(dst));
    aux_export(s, AuxFolder::pictures, "x.png", dst);
    REQUIRE(read_all(dst) == read_all(src));  // overwritten
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement `aux_export`**

```cpp
void aux_export(const ProjectState&            s,
                AuxFolder                      folder,
                const std::string&             name,
                const boost::filesystem::path& to)
{
    boost::system::error_code ec;
    auto aux_root = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto src = aux_root / folder_subdir(folder) / name;
    if (!boost::filesystem::is_regular_file(src, ec))
        throw std::out_of_range("aux entry not found: "
            + std::string(folder_flag(folder)) + "/" + name);

    // Resolve destination: if `to` is an existing directory, write to to/name.
    boost::filesystem::path dst = to;
    if (boost::filesystem::is_directory(to, ec))
        dst = to / name;

    auto parent = dst.parent_path();
    if (!parent.empty() && !boost::filesystem::is_directory(parent, ec))
        throw std::invalid_argument("--to parent dir does not exist: "
                                    + parent.string());

    boost::filesystem::copy_file(src, dst,
        boost::filesystem::copy_options::overwrite_existing, ec);
    if (ec) throw std::runtime_error("failed to export aux file: " + ec.message());
}
```

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_tab_ops.cpp tests/cli/unit/test_project_tab_ops.cpp
git commit -m "feat(cli): aux_export (file dest, dir dest, missing-parent, overwrite)

+5 unit cases. If --to is a directory, writes to <to>/<name>. Missing
parent dir throws std::invalid_argument (no mkdir). Missing entry
throws std::out_of_range. Existing destination is overwritten without
prompting (consent via explicit --to)."
```

---

## Task 17: Refactor `project_init.cpp` so the `project` parent subcommand can host more leaves

**Files:**
- Modify: `src/cli/commands/project_init.hpp`
- Modify: `src/cli/commands/project_init.cpp`

This is a structural prep step. The existing `register_project_subcmd` creates the `project` parent AND registers `init` in one function. We split that: the parent stays in `register_project_subcmd`, the leaf moves into `install_project_init_subcmd`. The leaf becomes addable from `commands/project_tab.cpp` in Task 18+.

- [ ] **Step 1: Update `commands/project_init.hpp`**

Add a new declaration alongside the existing one. The existing `register_project_subcmd(CLI::App& app, GlobalOpts& g)` stays for backward compatibility with `main.cpp`.

```cpp
#pragma once
#include "../globals.hpp"
namespace CLI { class App; }

namespace orca_cli::commands {

// Creates the `project` parent subcommand and installs all subverb families.
// Called once from main.cpp.
void register_project_subcmd(CLI::App& app, GlobalOpts& g);

// Installs ONE subverb family under an existing `project` parent. Each leaf
// installer is responsible for its own subverb registrations + callbacks.
// Used internally by register_project_subcmd; exposed here so project_tab.cpp
// can be a separate translation unit.
void install_project_init_subcmd   (CLI::App& project, GlobalOpts& g);
void install_project_info_subcmd   (CLI::App& project, GlobalOpts& g);
void install_project_profile_subcmd(CLI::App& project, GlobalOpts& g);
void install_project_aux_subcmd    (CLI::App& project, GlobalOpts& g);

} // namespace orca_cli::commands
```

- [ ] **Step 2: Update `commands/project_init.cpp`**

Restructure: extract the existing init body into `install_project_init_subcmd`; have `register_project_subcmd` create the parent and call all four installers. The new installers (info/profile/aux) live in `commands/project_tab.cpp` — Step 3 below adds it with empty stub bodies so the link is clean immediately. The real bodies arrive in Tasks 18-20.

Replace the existing `register_project_subcmd`:

```cpp
void install_project_init_subcmd(CLI::App& project, GlobalOpts& g)
{
    auto* init = project.add_subcommand(
        "init", "clone a reference 3mf into a new project");

    static std::string out, tmpl;
    init->add_option("out", out, "destination .3mf path")->required();
    init->add_option("--template", tmpl, "reference .3mf to clone")->required();

    init->callback([&g]() {
        std::exit(do_project_init(g, out, tmpl));
    });
}

void register_project_subcmd(CLI::App& app, GlobalOpts& g)
{
    auto* project = app.add_subcommand("project", "project-level operations");
    install_project_init_subcmd   (*project, g);
    install_project_info_subcmd   (*project, g);
    install_project_profile_subcmd(*project, g);
    install_project_aux_subcmd    (*project, g);
}
```

- [ ] **Step 3: Stub `commands/project_tab.{hpp,cpp}` so the link is clean**

Create `src/cli/commands/project_tab.hpp`:

```cpp
#pragma once
#include "../globals.hpp"
namespace CLI { class App; }

namespace orca_cli::commands {

// Bodies for the three project-tab subverb families. Declarations also live
// in project_init.hpp so register_project_subcmd can dispatch — included
// here for any future caller that wants just one family.
void install_project_info_subcmd   (CLI::App& project, GlobalOpts& g);
void install_project_profile_subcmd(CLI::App& project, GlobalOpts& g);
void install_project_aux_subcmd    (CLI::App& project, GlobalOpts& g);

} // namespace orca_cli::commands
```

Create `src/cli/commands/project_tab.cpp` with empty bodies:

```cpp
#include "project_tab.hpp"
#include "../cli11/CLI11.hpp"

namespace orca_cli::commands {

void install_project_info_subcmd   (CLI::App& /*project*/, GlobalOpts& /*g*/) {}
void install_project_profile_subcmd(CLI::App& /*project*/, GlobalOpts& /*g*/) {}
void install_project_aux_subcmd    (CLI::App& /*project*/, GlobalOpts& /*g*/) {}

} // namespace orca_cli::commands
```

- [ ] **Step 4: Wire `project_tab.cpp` into CMake**

In `src/cli/CMakeLists.txt`, add `commands/project_tab.cpp` next to `commands/project_init.cpp`.

- [ ] **Step 5: Build and verify everything still passes**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" --order rand --warn NoAssertions
```

Expected: baseline count + the unit cases added so far in Tasks 4-16, all passing. `project init` still works (sanity-check `& build\src\Release\orca-cli.exe project --help` — you should see only `init` listed because the three new installers are still no-ops).

- [ ] **Step 6: Commit**

```powershell
git add src/cli/commands/project_init.hpp src/cli/commands/project_init.cpp src/cli/commands/project_tab.hpp src/cli/commands/project_tab.cpp src/cli/CMakeLists.txt
git commit -m "refactor(cli): split project subcmd registration into per-leaf installers

register_project_subcmd now creates the `project` parent and delegates
to four install_project_*_subcmd installers. The init installer keeps
its existing body; info/profile/aux installers are stubbed (real
bodies land in following commits)."
```

---

## Task 18: Wire `project info {show,set,clear}` verbs + e2e tests

**Files:**
- Modify: `src/cli/commands/project_tab.cpp`
- Create: `tests/cli/e2e/test_project_tab.cpp`
- Modify: `tests/cli/CMakeLists.txt`

- [ ] **Step 1: Create the e2e test file scaffold**

```cpp
// tests/cli/e2e/test_project_tab.cpp
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "io.hpp"
#include "project_ops.hpp"
#include "project_tab_ops.hpp"

#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>

using namespace orca_cli_test;
namespace fs = boost::filesystem;

// Helper: parse the JSON envelope emitted by --json mode.
static nlohmann::json parse_json_envelope(const std::string& out) {
    auto idx = out.find('{');
    REQUIRE(idx != std::string::npos);
    return nlohmann::json::parse(out.substr(idx));
}
```

- [ ] **Step 2: Wire it into CMake**

In `tests/cli/CMakeLists.txt`, add `e2e/test_project_tab.cpp` to the `cli_tests` sources.

- [ ] **Step 3: Add the failing e2e tests for `project info`**

Append to `tests/cli/e2e/test_project_tab.cpp`:

```cpp
TEST_CASE("orca-cli: project info set + show --json round-trips fields",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-set");

    auto r = run_cli({"project", "info", "set", in.string(),
                      "--title", "Smoke",
                      "--description", "auto-set",
                      "--license", "MIT"});
    INFO("set stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto r2 = run_cli({"--json", "project", "info", "show", in.string()});
    INFO("show stdout: " << r2.stdout_ << "\nstderr: " << r2.stderr_);
    REQUIRE(r2.exit_code == 0);
    auto j = parse_json_envelope(r2.stdout_);
    REQUIRE(j["data"]["title"]       == "Smoke");
    REQUIRE(j["data"]["description"] == "auto-set");
    REQUIRE(j["data"]["license"]     == "MIT");
}

TEST_CASE("orca-cli: project info set with zero field flags is usage error",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-set-zero");
    auto r = run_cli({"project", "info", "set", in.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == int(orca_cli::ExitCode::usage_error));
}

TEST_CASE("orca-cli: project info show --json on pristine 3mf emits stable shape",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-show-pristine");
    auto r = run_cli({"--json", "project", "info", "show", in.string()});
    REQUIRE(r.exit_code == 0);
    auto j = parse_json_envelope(r.stdout_);
    for (const auto* k : {"title","description","license","copyright","cover","origin"})
        REQUIRE(j["data"].contains(k));
}

TEST_CASE("orca-cli: project info clear nulls named fields",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-clear");
    REQUIRE(run_cli({"project", "info", "set", in.string(),
                     "--title", "T", "--description", "D"}).exit_code == 0);
    REQUIRE(run_cli({"project", "info", "clear", in.string(),
                     "--field", "title,description"}).exit_code == 0);
    auto r = run_cli({"--json", "project", "info", "show", in.string()});
    auto j = parse_json_envelope(r.stdout_);
    REQUIRE(j["data"]["title"]       == "");
    REQUIRE(j["data"]["description"] == "");
}

TEST_CASE("orca-cli: project info set --output O writes to O and leaves input untouched",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-output");
    auto out = tmp / "copy.3mf";

    // Snapshot the input bytes so we can verify it wasn't touched.
    std::ifstream sf(in.string(), std::ios::binary);
    std::vector<unsigned char> input_before(
        std::istreambuf_iterator<char>(sf), std::istreambuf_iterator<char>{});

    auto r = run_cli({"project", "info", "set", in.string(),
                      "--title", "OutputSmoke",
                      "--output", out.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    // (a) --output path exists and carries the mutation.
    REQUIRE(fs::exists(out));
    auto r2 = run_cli({"--json", "project", "info", "show", out.string()});
    REQUIRE(r2.exit_code == 0);
    auto j = parse_json_envelope(r2.stdout_);
    REQUIRE(j["data"]["title"] == "OutputSmoke");

    // (b) input file is byte-equal to before.
    std::ifstream af(in.string(), std::ios::binary);
    std::vector<unsigned char> input_after(
        std::istreambuf_iterator<char>(af), std::istreambuf_iterator<char>{});
    REQUIRE(input_before == input_after);
}
```

- [ ] **Step 4: Run; verify failures (the verbs don't exist yet)**

The unknown subverb will surface as CLI11 `usage_error`.

- [ ] **Step 5: Implement `install_project_info_subcmd` in `commands/project_tab.cpp`**

Replace the empty body with real wiring:

```cpp
#include "project_tab.hpp"
#include "mutation_runner.hpp"
#include "../cli11/CLI11.hpp"
#include "../io.hpp"
#include "../output.hpp"
#include "../project_tab_ops.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace orca_cli::commands {

namespace {

// -- info show -------------------------------------------------------------

int do_info_show(const GlobalOpts& g, const std::string& file)
{
    // Belt-and-suspenders: --output isn't registered on this subcommand
    // (CLI11 would reject it as "unknown option" → usage_error), but if a
    // future refactor re-exposes it accidentally we surface a clear
    // diagnostic instead of silently ignoring the flag.
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error, "project info show does not accept --output");
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok)) return rc;

    ProjectState state;
    try {
        state = load_project(file);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    auto v = info_view(state);
    if (g.json) {
        nlohmann::json data{
            {"title",       v.title},
            {"description", v.description},
            {"license",     v.license},
            {"copyright",   v.copyright},
            {"cover",       v.cover},
            {"origin",      v.origin},
        };
        print_ok(g, "info", data);
    } else {
        std::fputs(("title:       " + v.title       + "\n").c_str(), stdout);
        std::fputs(("description: " + v.description + "\n").c_str(), stdout);
        std::fputs(("license:     " + v.license     + "\n").c_str(), stdout);
        std::fputs(("copyright:   " + v.copyright   + "\n").c_str(), stdout);
        std::fputs(("cover:       " + v.cover       + "\n").c_str(), stdout);
        std::fputs(("origin:      " + v.origin      + "\n").c_str(), stdout);
        std::fflush(stdout);
    }
    return int(ExitCode::ok);
}

// -- info set --------------------------------------------------------------

int do_info_set(const GlobalOpts& g, const std::string& file, const InfoSetParams& p)
{
    if (!any_field_set(p)) {
        print_err(g, ExitCode::usage_error,
                  "project info set requires at least one of --title/--description/"
                  "--license/--copyright/--cover");
        return int(ExitCode::usage_error);
    }
    MutationExceptionMap em;
    em.on<BadCoverImage>(ExitCode::bad_config);
    return run_mutation(g, file, "info set applied", em,
        [&](ProjectState& s) { info_set(s, p); });
}

// -- info clear ------------------------------------------------------------

int do_info_clear(const GlobalOpts& g, const std::string& file,
                  const std::vector<std::string>& fields)
{
    MutationExceptionMap em;
    em.on<InvalidField>(ExitCode::bad_config);
    return run_mutation(g, file, "info cleared", em,
        [&](ProjectState& s) { info_clear(s, fields); });
}

// Split a comma-separated string into trimmed tokens. Empty tokens dropped.
std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

} // namespace

void install_project_info_subcmd(CLI::App& project, GlobalOpts& g)
{
    auto* info = project.add_subcommand("info", "model-level metadata (Project tab)");

    // -- show --------------------------------------------------------------
    auto* show = info->add_subcommand("show", "print model metadata fields");
    static std::string show_file;
    show->add_option("file", show_file, "input .3mf path")->required();
    // Read-only verb: --output is intentionally NOT registered here so
    // CLI11 rejects it as an unknown option (cleaner --help; the runtime
    // guard in do_info_show is defense-in-depth).
    show->callback([&g]() { std::exit(do_info_show(g, show_file)); });

    // -- set ---------------------------------------------------------------
    auto* set = info->add_subcommand("set", "set one or more model metadata fields");
    static std::string set_file;
    static std::string s_title, s_desc, s_license, s_copyright, s_cover;
    static bool h_title=false, h_desc=false, h_license=false, h_copyright=false, h_cover=false;
    set->add_option("file", set_file, "input .3mf path")->required();
    auto* o_t = set->add_option("--title",       s_title,     "model title")->each([&](const std::string&){h_title=true;});
    auto* o_d = set->add_option("--description", s_desc,      "model description")->each([&](const std::string&){h_desc=true;});
    auto* o_l = set->add_option("--license",     s_license,   "model license")->each([&](const std::string&){h_license=true;});
    auto* o_c = set->add_option("--copyright",   s_copyright, "model copyright (JSON array string)")->each([&](const std::string&){h_copyright=true;});
    auto* o_v = set->add_option("--cover",       s_cover,     "PNG cover image (file path)")->each([&](const std::string&){h_cover=true;});
    (void)o_t; (void)o_d; (void)o_l; (void)o_c; (void)o_v;
    set->add_option("--output", g.output,
                    "write result to this path instead of overwriting input");
    set->callback([&g]() {
        InfoSetParams p;
        if (h_title)     p.title       = s_title;
        if (h_desc)      p.description = s_desc;
        if (h_license)   p.license     = s_license;
        if (h_copyright) p.copyright   = s_copyright;
        if (h_cover)     p.cover       = boost::filesystem::path(s_cover);
        int rc = do_info_set(g, set_file, p);
        // Reset flags for the next parse (statics live forever).
        h_title=h_desc=h_license=h_copyright=h_cover=false;
        std::exit(rc);
    });

    // -- clear -------------------------------------------------------------
    auto* clear = info->add_subcommand("clear", "null one or more model metadata fields");
    static std::string clear_file, clear_fields_csv;
    clear->add_option("file", clear_file, "input .3mf path")->required();
    clear->add_option("--field", clear_fields_csv,
                      "comma-separated field names (title,description,license,copyright,cover)")->required();
    clear->add_option("--output", g.output,
                      "write result to this path instead of overwriting input");
    clear->callback([&g]() {
        auto fields = split_csv(clear_fields_csv);
        std::exit(do_info_clear(g, clear_file, fields));
    });
}

void install_project_profile_subcmd(CLI::App& /*project*/, GlobalOpts& /*g*/) {}
void install_project_aux_subcmd    (CLI::App& /*project*/, GlobalOpts& /*g*/) {}

} // namespace orca_cli::commands
```

> **Implementer note:** CLI11's "did the user pass this option" detection is normally `option->count() > 0`. The `->each()` trick above is a workaround that compiles cleanly across all CLI11 versions in the cli11/ vendored tree. If your CLI11 version supports `set->get_option("--title")->count()`, prefer that — refactor to it before this commit if it works on your tree.

- [ ] **Step 6: Re-run; verify e2e tests pass**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][project-tab][e2e]" --order rand --warn NoAssertions
```

- [ ] **Step 7: Commit**

```powershell
git add src/cli/commands/project_tab.cpp tests/cli/e2e/test_project_tab.cpp tests/cli/CMakeLists.txt
git commit -m "feat(cli): wire project info {show,set,clear} verbs

+5 e2e cases. show emits human-table or --json envelope (stable
six-key shape). set requires >=1 field flag (usage_error otherwise);
batches via InfoSetParams; --cover routes through embed_cover_image
(BadCoverImage maps to bad_config); --output writes a copy without
touching the input. clear takes comma-separated --field list;
InvalidField -> bad_config; idempotent."
```

---

## Task 19: Wire `project profile {show,set,clear}` verbs + e2e tests

**Files:**
- Modify: `src/cli/commands/project_tab.cpp`
- Modify: `tests/cli/e2e/test_project_tab.cpp`

Symmetric to Task 18. Lift the helpers (`split_csv`, etc.) — they're already in the anonymous namespace.

- [ ] **Step 1: Add the failing e2e test**

```cpp
TEST_CASE("orca-cli: project profile set + show --json round-trips fields",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "profile-set");
    REQUIRE(run_cli({"project", "profile", "set", in.string(),
                     "--title", "PT", "--description", "PD"}).exit_code == 0);

    auto r = run_cli({"--json", "project", "profile", "show", in.string()});
    INFO("show stdout: " << r.stdout_);
    REQUIRE(r.exit_code == 0);
    auto j = parse_json_envelope(r.stdout_);
    REQUIRE(j["data"]["title"]       == "PT");
    REQUIRE(j["data"]["description"] == "PD");
    REQUIRE(j["data"].contains("user_id"));    // read-only, always present
    REQUIRE(j["data"].contains("user_name"));
}
```

- [ ] **Step 2: Run; verify failure**

- [ ] **Step 3: Implement `install_project_profile_subcmd`**

In `commands/project_tab.cpp`, replace the empty body:

```cpp
namespace {
int do_profile_show(const GlobalOpts& g, const std::string& file)
{
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error, "project profile show does not accept --output");
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok)) return rc;

    ProjectState state;
    try {
        state = load_project(file);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    auto v = profile_view(state);
    if (g.json) {
        nlohmann::json data{
            {"title",       v.title},
            {"description", v.description},
            {"cover",       v.cover},
            {"user_id",     v.user_id},
            {"user_name",   v.user_name},
        };
        print_ok(g, "profile", data);
    } else {
        std::fputs(("title:       " + v.title       + "\n").c_str(), stdout);
        std::fputs(("description: " + v.description + "\n").c_str(), stdout);
        std::fputs(("cover:       " + v.cover       + "\n").c_str(), stdout);
        std::fputs(("user_id:     " + v.user_id     + "\n").c_str(), stdout);
        std::fputs(("user_name:   " + v.user_name   + "\n").c_str(), stdout);
        std::fflush(stdout);
    }
    return int(ExitCode::ok);
}

int do_profile_set(const GlobalOpts& g, const std::string& file, const ProfileSetParams& p)
{
    if (!any_field_set(p)) {
        print_err(g, ExitCode::usage_error,
                  "project profile set requires at least one of --title/--description/--cover");
        return int(ExitCode::usage_error);
    }
    MutationExceptionMap em;
    em.on<BadCoverImage>(ExitCode::bad_config);
    return run_mutation(g, file, "profile set applied", em,
        [&](ProjectState& s) { profile_set(s, p); });
}

int do_profile_clear(const GlobalOpts& g, const std::string& file,
                     const std::vector<std::string>& fields)
{
    MutationExceptionMap em;
    em.on<InvalidField>(ExitCode::bad_config);
    return run_mutation(g, file, "profile cleared", em,
        [&](ProjectState& s) { profile_clear(s, fields); });
}
} // namespace

void install_project_profile_subcmd(CLI::App& project, GlobalOpts& g)
{
    auto* prof = project.add_subcommand("profile", "print-profile metadata (Project tab)");

    auto* show = prof->add_subcommand("show", "print profile metadata fields");
    static std::string show_file;
    show->add_option("file", show_file, "input .3mf path")->required();
    // Read-only verb: --output intentionally NOT registered (see info show).
    show->callback([&g]() { std::exit(do_profile_show(g, show_file)); });

    auto* set = prof->add_subcommand("set", "set one or more profile metadata fields");
    static std::string set_file, s_title, s_desc, s_cover;
    static bool h_title=false, h_desc=false, h_cover=false;
    set->add_option("file", set_file, "input .3mf path")->required();
    set->add_option("--title",       s_title, "profile title")->each([&](const std::string&){h_title=true;});
    set->add_option("--description", s_desc,  "profile description")->each([&](const std::string&){h_desc=true;});
    set->add_option("--cover",       s_cover, "PNG cover image (file path)")->each([&](const std::string&){h_cover=true;});
    set->add_option("--output", g.output,
                    "write result to this path instead of overwriting input");
    set->callback([&g]() {
        ProfileSetParams p;
        if (h_title) p.title       = s_title;
        if (h_desc)  p.description = s_desc;
        if (h_cover) p.cover       = boost::filesystem::path(s_cover);
        int rc = do_profile_set(g, set_file, p);
        h_title=h_desc=h_cover=false;
        std::exit(rc);
    });

    auto* clear = prof->add_subcommand("clear", "null one or more profile metadata fields");
    static std::string clear_file, clear_fields_csv;
    clear->add_option("file", clear_file, "input .3mf path")->required();
    clear->add_option("--field", clear_fields_csv,
                      "comma-separated field names (title,description,cover)")->required();
    clear->add_option("--output", g.output,
                      "write result to this path instead of overwriting input");
    clear->callback([&g]() {
        auto fields = split_csv(clear_fields_csv);
        std::exit(do_profile_clear(g, clear_file, fields));
    });
}
```

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/commands/project_tab.cpp tests/cli/e2e/test_project_tab.cpp
git commit -m "feat(cli): wire project profile {show,set,clear} verbs

+1 e2e case. Symmetric to info: same usage_error / bad_config /
invariant_violation mapping; ProfileSetParams batches; user_id and
user_name surface read-only in show envelope."
```

---

## Task 20: Wire `project aux {list,add,remove,export}` verbs + e2e tests (incl. JSON naming convention regression)

**Files:**
- Modify: `src/cli/commands/project_tab.cpp`
- Modify: `tests/cli/e2e/test_project_tab.cpp`
- Create: `tests/cli/fixtures/assembly_smoke.txt` (placeholder; real file lands in Task 22)

For now the e2e tests can use any small file (e.g. the existing `two_cubes.stl`) as the aux payload. The real fixture files (Task 22) replace the throwaway sources before final test run.

- [ ] **Step 1: Add the failing e2e tests**

```cpp
TEST_CASE("orca-cli: project aux add then list reports under correct bucket",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-add");

    REQUIRE(run_cli({"project", "aux", "add", in.string(),
                     "--folder", "others",
                     "--file",   cube.string(),
                     "--name",   "sample.stl"}).exit_code == 0);

    auto r = run_cli({"--json", "project", "aux", "list", in.string()});
    INFO("list stdout: " << r.stdout_);
    REQUIRE(r.exit_code == 0);
    auto j = parse_json_envelope(r.stdout_);
    // Stable shape: every bucket key present (spec § 2.2)
    REQUIRE(j["data"].contains("pictures"));
    REQUIRE(j["data"].contains("bom"));
    REQUIRE(j["data"].contains("assembly_guide"));  // underscore — spec § 2.3
    REQUIRE(j["data"].contains("others"));
    bool saw = false;
    for (const auto& e : j["data"]["others"])
        if (e["name"] == "sample.stl") saw = true;
    REQUIRE(saw);
}

TEST_CASE("orca-cli: project aux add --folder uses hyphen (assembly-guide)",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-assembly");

    REQUIRE(run_cli({"project", "aux", "add", in.string(),
                     "--folder", "assembly-guide",   // hyphen on flag
                     "--file",   cube.string(),
                     "--name",   "instructions.bin"}).exit_code == 0);

    auto r = run_cli({"--json", "project", "aux", "list", in.string()});
    auto j = parse_json_envelope(r.stdout_);
    bool saw = false;
    for (const auto& e : j["data"]["assembly_guide"])    // underscore in JSON
        if (e["name"] == "instructions.bin") saw = true;
    REQUIRE(saw);
}

TEST_CASE("orca-cli: project aux add collision is exit 5; --force is exit 0",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-collide");
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","x.bin"}).exit_code == 0);
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","x.bin"}).exit_code
            == int(orca_cli::ExitCode::duplicate_name));
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","x.bin","--force"}).exit_code == 0);
}

TEST_CASE("orca-cli: project aux add --name CON.png is exit 4 (bad_config)",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-cn");
    auto r = run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","CON.png"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == int(orca_cli::ExitCode::bad_config));
}

TEST_CASE("orca-cli: project aux remove missing-name is exit 6",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-rm-missing");
    auto r = run_cli({"project","aux","remove",in.string(),"--folder","pictures","--name","never_there.png"});
    REQUIRE(r.exit_code == int(orca_cli::ExitCode::unknown_reference));
}

TEST_CASE("orca-cli: project aux export to file and to directory destinations",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-export");
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","x.bin"}).exit_code == 0);

    auto out_dir = make_temp_dir();
    auto file_dst = out_dir / "renamed.bin";
    REQUIRE(run_cli({"project","aux","export",in.string(),"--folder","others","--name","x.bin","--to",file_dst.string()}).exit_code == 0);
    REQUIRE(fs::exists(file_dst));

    auto dir_dst = make_temp_dir();
    REQUIRE(run_cli({"project","aux","export",in.string(),"--folder","others","--name","x.bin","--to",dir_dst.string()}).exit_code == 0);
    REQUIRE(fs::exists(dir_dst / "x.bin"));
}

TEST_CASE("orca-cli: project aux export to non-existent --to parent is exit 4",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-export-bad");
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","x.bin"}).exit_code == 0);
    auto bad = make_temp_dir() / "no_such_dir" / "out.bin";
    REQUIRE(run_cli({"project","aux","export",in.string(),"--folder","others","--name","x.bin","--to",bad.string()}).exit_code
            == int(orca_cli::ExitCode::bad_config));
}
```

- [ ] **Step 2: Run; verify failures**

- [ ] **Step 3: Implement `install_project_aux_subcmd`**

```cpp
namespace {

// Map --folder string to AuxFolder enum, or throw a CLI11 ValidationError.
// (CLI11's CheckedTransformer would also work; this is explicit.)
AuxFolder parse_folder(const std::string& s) {
    if (s == "pictures")       return AuxFolder::pictures;
    if (s == "bom")            return AuxFolder::bom;
    if (s == "assembly-guide") return AuxFolder::assembly_guide;
    if (s == "others")         return AuxFolder::others;
    throw CLI::ValidationError("--folder",
        "must be one of: pictures, bom, assembly-guide, others (got '" + s + "')");
}

int do_aux_list(const GlobalOpts& g, const std::string& file)
{
    // Belt-and-suspenders (see do_info_show comment): --output isn't
    // registered on aux list, but guard anyway.
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error, "project aux list does not accept --output");
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok)) return rc;

    ProjectState state;
    try {
        state = load_project(file);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    auto entries = aux_list(state);
    if (g.json) {
        nlohmann::json data{
            {"pictures",       nlohmann::json::array()},
            {"bom",            nlohmann::json::array()},
            {"assembly_guide", nlohmann::json::array()},
            {"others",         nlohmann::json::array()},
        };
        for (const auto& e : entries)
            data[folder_json_key(e.folder)].push_back({{"name", e.name}, {"size", e.size}});
        print_ok(g, "aux", data);
    } else {
        for (const auto& e : entries) {
            std::string line = std::string(folder_flag(e.folder)) + "/" + e.name
                             + "  " + std::to_string(e.size) + " B\n";
            std::fputs(line.c_str(), stdout);
        }
        std::fflush(stdout);
    }
    return int(ExitCode::ok);
}

int do_aux_add(const GlobalOpts& g, const std::string& file,
               const std::string& folder, const std::string& src,
               const std::string& name, bool force)
{
    AuxFolder f;
    try { f = parse_folder(folder); }
    catch (const CLI::ValidationError& e) {
        print_err(g, ExitCode::usage_error, e.what());
        return int(ExitCode::usage_error);
    }
    MutationExceptionMap em;
    em.on<BadAuxFile>(ExitCode::file_not_found);
    em.on<AuxNameError>(ExitCode::bad_config);
    em.on<AuxCollisionError>(ExitCode::duplicate_name);

    AuxAddParams p;
    p.folder = f;
    p.file   = boost::filesystem::path(src);
    if (!name.empty()) p.name = name;
    p.force  = force;
    return run_mutation(g, file, "aux added", em,
        [&](ProjectState& s) { aux_add(s, p); });
}

int do_aux_remove(const GlobalOpts& g, const std::string& file,
                  const std::string& folder, const std::string& name)
{
    AuxFolder f;
    try { f = parse_folder(folder); }
    catch (const CLI::ValidationError& e) {
        print_err(g, ExitCode::usage_error, e.what());
        return int(ExitCode::usage_error);
    }
    MutationExceptionMap em;  // default out_of_range -> unknown_reference
    return run_mutation(g, file, "aux removed", em,
        [&](ProjectState& s) { aux_remove(s, f, name); });
}

int do_aux_export(const GlobalOpts& g, const std::string& file,
                  const std::string& folder, const std::string& name,
                  const std::string& to)
{
    // Belt-and-suspenders (see do_info_show comment): --output is not
    // registered on aux export (which uses --to instead), but guard anyway.
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error, "project aux export does not accept --output (use --to)");
        return int(ExitCode::usage_error);
    }
    AuxFolder f;
    try { f = parse_folder(folder); }
    catch (const CLI::ValidationError& e) {
        print_err(g, ExitCode::usage_error, e.what());
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok)) return rc;

    ProjectState state;
    try {
        state = load_project(file);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    try {
        aux_export(state, f, name, boost::filesystem::path(to));
    } catch (const std::out_of_range& e) {
        print_err(g, ExitCode::unknown_reference, e.what());
        return int(ExitCode::unknown_reference);
    } catch (const std::invalid_argument& e) {
        print_err(g, ExitCode::bad_config, e.what());
        return int(ExitCode::bad_config);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    print_ok(g, "aux exported");
    return int(ExitCode::ok);
}
} // namespace

void install_project_aux_subcmd(CLI::App& project, GlobalOpts& g)
{
    auto* aux = project.add_subcommand("aux", "auxiliary file attachments (Project tab)");

    auto* list = aux->add_subcommand("list", "list aux entries by bucket");
    static std::string list_file;
    list->add_option("file", list_file, "input .3mf path")->required();
    // Read-only verb: --output intentionally NOT registered (see info show).
    list->callback([&g]() { std::exit(do_aux_list(g, list_file)); });

    auto* add = aux->add_subcommand("add", "attach a file to an aux bucket");
    static std::string add_file, add_folder, add_src, add_name;
    static bool add_force = false;
    add->add_option("file", add_file, "input .3mf path")->required();
    add->add_option("--folder", add_folder,
                    "pictures | bom | assembly-guide | others")->required();
    add->add_option("--file",   add_src,  "source file on disk")->required();
    add->add_option("--name",   add_name, "override in-3mf basename");
    add->add_flag  ("--force",  add_force,
                    "overwrite an existing aux entry with the same name");
    add->add_option("--output", g.output,
                    "write result to this path instead of overwriting input");
    add->callback([&g]() {
        int rc = do_aux_add(g, add_file, add_folder, add_src, add_name, add_force);
        add_force = false; add_name.clear();
        std::exit(rc);
    });

    auto* rem = aux->add_subcommand("remove", "delete an aux entry");
    static std::string rem_file, rem_folder, rem_name;
    rem->add_option("file", rem_file, "input .3mf path")->required();
    rem->add_option("--folder", rem_folder,
                    "pictures | bom | assembly-guide | others")->required();
    rem->add_option("--name",   rem_name, "in-3mf basename to remove")->required();
    rem->add_option("--output", g.output,
                    "write result to this path instead of overwriting input");
    rem->callback([&g]() { std::exit(do_aux_remove(g, rem_file, rem_folder, rem_name)); });

    auto* exp = aux->add_subcommand("export", "copy an aux entry out to disk");
    static std::string exp_file, exp_folder, exp_name, exp_to;
    exp->add_option("file", exp_file, "input .3mf path")->required();
    exp->add_option("--folder", exp_folder,
                    "pictures | bom | assembly-guide | others")->required();
    exp->add_option("--name",   exp_name, "in-3mf basename to export")->required();
    exp->add_option("--to",     exp_to,   "destination file or directory")->required();
    exp->callback([&g]() { std::exit(do_aux_export(g, exp_file, exp_folder, exp_name, exp_to)); });
}
```

- [ ] **Step 4: Re-run; verify pass**

- [ ] **Step 5: Commit**

```powershell
git add src/cli/commands/project_tab.cpp tests/cli/e2e/test_project_tab.cpp
git commit -m "feat(cli): wire project aux {list,add,remove,export} verbs

+7 e2e cases. list emits stable four-bucket JSON shape with
underscore keys (assembly_guide); --folder flag uses hyphens. add
sanitizes --name, refuses collision without --force, --force=success.
remove maps std::out_of_range to unknown_reference. export resolves
--to as file-or-directory, refuses missing parent (bad_config),
overwrites without prompting."
```

---

## Task 21: Add roundtrip tests (3 cases)

**Files:**
- Create: `tests/cli/roundtrip/test_project_tab.cpp`
- Modify: `tests/cli/CMakeLists.txt`

Per spec § 6.3: (a) full set survives pack/unpack; (b) cover bytes + pointer survive; (c) add+remove leaves bucket empty.

- [ ] **Step 1: Create the file and wire it into CMake**

```cpp
// tests/cli/roundtrip/test_project_tab.cpp
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "io.hpp"
#include "project_ops.hpp"
#include "project_tab_ops.hpp"

#include <boost/filesystem.hpp>
#include <fstream>

using namespace orca_cli;
using namespace orca_cli_test;
namespace fs = boost::filesystem;

static std::vector<unsigned char> read_all(const fs::path& p) {
    std::ifstream f(p.string(), std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),
                                      std::istreambuf_iterator<char>{});
}
```

Add `roundtrip/test_project_tab.cpp` to `tests/cli/CMakeLists.txt`.

- [ ] **Step 2: Add the three roundtrip tests**

```cpp
TEST_CASE("orca-cli: project info+profile+aux survive save/load roundtrip",
          "[orca-cli][project-tab][roundtrip]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "ptab-rt");
    REQUIRE(run_cli({"project","info","set",in.string(),
                     "--title","RT","--description","D","--license","MIT"}).exit_code == 0);
    REQUIRE(run_cli({"project","profile","set",in.string(),
                     "--title","RPT","--description","RPD"}).exit_code == 0);
    REQUIRE(run_cli({"project","aux","add",in.string(),
                     "--folder","others","--file",cube.string(),"--name","x.bin"}).exit_code == 0);

    auto s = load_project(in.string());
    auto iv = info_view(s);
    auto pv = profile_view(s);
    REQUIRE(iv.title       == "RT");
    REQUIRE(iv.description == "D");
    REQUIRE(iv.license     == "MIT");
    REQUIRE(pv.title       == "RPT");
    REQUIRE(pv.description == "RPD");
    auto entries = aux_list(s);
    bool saw = false;
    for (const auto& e : entries)
        if (e.folder == AuxFolder::others && e.name == "x.bin") saw = true;
    REQUIRE(saw);
}

TEST_CASE("orca-cli: project info set --cover survives save/load (pointer + bytes)",
          "[orca-cli][project-tab][roundtrip]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto png = fs::path(ORCA_CLI_FIXTURES_DIR) / "cover_smoke.png";
    if (!fs::exists(png)) { SUCCEED("Skipped: cover_smoke.png missing"); return; }
    auto src_bytes = read_all(png);
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "ptab-cover-rt");

    REQUIRE(run_cli({"project","info","set",in.string(),"--cover",png.string()}).exit_code == 0);

    auto s = load_project(in.string());
    REQUIRE(s.model->model_info != nullptr);
    REQUIRE(s.model->model_info->cover_file
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    auto landed = fs::path(s.model->get_auxiliary_file_temp_path())
                  / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(fs::exists(landed));
    REQUIRE(read_all(landed) == src_bytes);
}

TEST_CASE("orca-cli: project aux add then remove leaves bucket empty after roundtrip",
          "[orca-cli][project-tab][roundtrip]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "ptab-aux-rt");
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","pictures","--file",cube.string(),"--name","x.png"}).exit_code == 0);
    REQUIRE(run_cli({"project","aux","remove",in.string(),"--folder","pictures","--name","x.png"}).exit_code == 0);

    auto s = load_project(in.string());
    auto entries = aux_list(s);
    for (const auto& e : entries)
        REQUIRE(!(e.folder == AuxFolder::pictures && e.name == "x.png"));
}
```

- [ ] **Step 3: Build and run; verify pass**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][project-tab][roundtrip]" --order rand --warn NoAssertions
```

- [ ] **Step 4: Commit**

```powershell
git add tests/cli/roundtrip/test_project_tab.cpp tests/cli/CMakeLists.txt
git commit -m "test(cli): roundtrip cases for project-tab edits

+3 cases per spec § 6.3. info/profile/aux survive pack/unpack;
--cover bytes + pointer survive (regression for embed_cover_image
PNG pass-through); aux add+remove leaves the bucket empty."
```

---

## Task 22: Generate the three fixture files and commit them

**Files:**
- Create: `tests/cli/fixtures/cover_smoke.png`
- Create: `tests/cli/fixtures/cover_smoke.jpg`
- Create: `tests/cli/fixtures/assembly_smoke.txt`

The PNG is the 67-byte handcrafted 1×1 transparent PNG used in unit-test fixtures (Task 9). Use that same byte sequence so the unit tests and the roundtrip test share the same fixture.

- [ ] **Step 1: Generate `cover_smoke.png` in PowerShell**

```powershell
$bytes = [byte[]](
  0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,
  0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
  0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
  0x08,0x06,0x00,0x00,0x00,0x1F,0x15,0xC4,
  0x89,0x00,0x00,0x00,0x0D,0x49,0x44,0x41,
  0x54,0x78,0x9C,0x63,0x00,0x01,0x00,0x00,
  0x05,0x00,0x01,0x0D,0x0A,0x2D,0xB4,0x00,
  0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,
  0x42,0x60,0x82
)
[System.IO.File]::WriteAllBytes(
  "tests\cli\fixtures\cover_smoke.png", $bytes)
```

Verify: `(Get-Item tests\cli\fixtures\cover_smoke.png).Length` should be `67`.

- [ ] **Step 2: Generate `cover_smoke.jpg`**

```powershell
# Minimal valid 1x1 JPEG (~125 bytes). Source: jpegtran -optimize on a
# 1x1 black JPEG; concrete bytes below produce a parseable image.
$bytes = [byte[]](
  0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,
  0x00,0x01,0x00,0x00,0xFF,0xDB,0x00,0x43,0x00,0x08,0x06,0x06,0x07,0x06,0x05,0x08,
  0x07,0x07,0x07,0x09,0x09,0x08,0x0A,0x0C,0x14,0x0D,0x0C,0x0B,0x0B,0x0C,0x19,0x12,
  0x13,0x0F,0x14,0x1D,0x1A,0x1F,0x1E,0x1D,0x1A,0x1C,0x1C,0x20,0x24,0x2E,0x27,0x20,
  0x22,0x2C,0x23,0x1C,0x1C,0x28,0x37,0x29,0x2C,0x30,0x31,0x34,0x34,0x34,0x1F,0x27,
  0x39,0x3D,0x38,0x32,0x3C,0x2E,0x33,0x34,0x32,0xFF,0xC0,0x00,0x0B,0x08,0x00,0x01,
  0x00,0x01,0x01,0x01,0x11,0x00,0xFF,0xC4,0x00,0x14,0x00,0x01,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xC4,0x00,0x14,
  0x10,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xFF,0xDA,0x00,0x08,0x01,0x01,0x00,0x00,0x3F,0x00,0xB2,0xC0,0x07,0xFF,0xD9
)
[System.IO.File]::WriteAllBytes(
  "tests\cli\fixtures\cover_smoke.jpg", $bytes)
```

Sanity-check that the JPG fails `is_png` (run `is_png rejects JPG signature` unit test from Task 8 against it if you want belt-and-suspenders).

- [ ] **Step 3: Generate `assembly_smoke.txt` (plain text — see decision note)**

The aux pipeline is byte-agnostic — it just copies the source file into
the chosen bucket — so the fixture format does not affect test
coverage. A hand-rolled minimal PDF is fragile (xref byte offsets are
position-sensitive and easy to get wrong; an invalid PDF would still
PASS the CLI tests but FAIL the Phase 10 GUI smoke when the GUI's
PDF viewer rejects it). A `.txt` file is unambiguously valid, renders
fine in the GUI's "Assembly Guide" tab (which shows attached files
as a list with download buttons, not inline-rendered), and exercises
exactly the same code paths.

```powershell
$txt = @"
ORCA-CLI PHASE 10 SMOKE FIXTURE
================================

This file exists to be attached to a .3mf via
'orca-cli project aux add --folder assembly-guide --file ...'
during the Phase 10 manual smoke recipe and the
[orca-cli][project-tab][e2e] suite.

It is not a real assembly guide. The byte content is irrelevant
to the test (the aux pipeline only copies bytes); the file is
plain UTF-8 text so it renders in any tool and never trips a
file-type validator.
"@
[System.IO.File]::WriteAllBytes(
  "tests\cli\fixtures\assembly_smoke.txt",
  [System.Text.Encoding]::UTF8.GetBytes($txt))
```

Verify: the file is non-empty and human-readable.

```powershell
Get-Content tests\cli\fixtures\assembly_smoke.txt | Select-Object -First 3
```

> **Note for executor:** if Phase 10 (Task 23) or any e2e test still
> references `assembly_smoke.pdf` after the rest of this revision
> landed, replace those references with `assembly_smoke.txt`. The plan
> (Task 23) and spec (§ 6.5) have already been updated.

- [ ] **Step 4: Make CMake export `ORCA_CLI_FIXTURES_DIR` (already done in v4 per `tests/cli/CMakeLists.txt`)**

The fixtures directory should already be exposed as the `ORCA_CLI_FIXTURES_DIR` compile-time define. If you find Task 21's roundtrip test fails to compile with "undeclared identifier ORCA_CLI_FIXTURES_DIR", check `tests/cli/CMakeLists.txt` and add:

```cmake
target_compile_definitions(cli_tests PRIVATE
    ORCA_CLI_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
```

(Adjust to match the actual existing macro definitions in that file.)

- [ ] **Step 5: Verify the full suite still passes**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" --order rand --warn NoAssertions
```

- [ ] **Step 6: Commit fixtures**

```powershell
git add tests/cli/fixtures/cover_smoke.png tests/cli/fixtures/cover_smoke.jpg tests/cli/fixtures/assembly_smoke.txt
git commit -m "test(cli): commit project-tab fixtures (cover PNG/JPG, assembly TXT)

cover_smoke.png    (67 B,  1x1 transparent PNG) — happy path for
                                                  info/profile --cover.
cover_smoke.jpg    (125 B, 1x1 black JPG)       — exercises
                                                  BadCoverImage rejection.
assembly_smoke.txt (~400 B, plain UTF-8 text)   — aux assembly-guide
                                                  happy path. TXT is
                                                  byte-equivalent for
                                                  the aux pipeline and
                                                  avoids the minimal-PDF
                                                  byte-offset fragility.
"
```

---

## Task 23: Append Phase 10 manual smoke recipe to `docs/cli/manual-test.md`

**Files:**
- Modify: `docs/cli/manual-test.md`

This is the GUI gate (spec § 6.4). It caught two production bugs in v2 per the project memory.

- [ ] **Step 1: Find the end of the file (the prior phase block)**

```powershell
Get-Content docs/cli/manual-test.md -Tail 30
```

You should see the Phase 9 cumulative block from v4.

- [ ] **Step 2: Append the Phase 10 section**

Add at the end of `docs/cli/manual-test.md`:

```markdown
## Phase 10 — Project tab editing (smoke)

This phase exercises the full `project info | profile | aux` surface
and ends with a GUI inspection. The reference 3mf must be untouched
before this recipe (copy first).

```powershell
$REF  = "tests\cli\fixtures\temp_project_for_orca_slicer.3mf"
$WORK = "$env:TEMP\orca-cli-phase10.3mf"
$PNG  = "tests\cli\fixtures\cover_smoke.png"
$TXT  = "tests\cli\fixtures\assembly_smoke.txt"
Copy-Item $REF $WORK -Force

# 1. Title + description + cover via info set
& build\src\Release\orca-cli.exe project info set $WORK `
    --title "Smoke Phase 10" `
    --description "Generated by Phase 10 recipe" `
    --cover $PNG

# 2. Profile metadata
& build\src\Release\orca-cli.exe project profile set $WORK `
    --title "Profile Smoke" `
    --description "Profile-side description"

# 3. Aux attachments: assembly guide + picture
& build\src\Release\orca-cli.exe project aux add $WORK `
    --folder assembly-guide --file $TXT --name "instructions.txt"
& build\src\Release\orca-cli.exe project aux add $WORK `
    --folder pictures --file $PNG --name "hero.png"

# 4. List to verify
& build\src\Release\orca-cli.exe --json project aux list $WORK
```

**GUI check.** Open `$WORK` in OrcaSlicer. Switch to the **Project**
tab. Verify:
- **Model:** title = "Smoke Phase 10"; description text visible;
  the small cover thumbnail at top-left shows the 1×1 PNG (will
  appear as a tiny solid square).
- **Profile section:** title = "Profile Smoke"; description visible.
- **Auxiliary panel (bottom):**
  - **Pictures** tab shows `hero.png`.
  - **Assembly Guide** tab shows `instructions.txt`.

**Cumulative recipe extension** (run after the Phase 9 cumulative
recipe in the same `$WORK`): the Phase 10 commands above can be
appended verbatim. The Phase 10 edits must not regress any Phase 9
behaviour — verify plates / objects / filaments still render
correctly in the GUI alongside the new metadata.
```

- [ ] **Step 3: Commit**

```powershell
git add docs/cli/manual-test.md
git commit -m "docs(cli): Phase 10 manual smoke recipe for project-tab verbs

End-to-end script + explicit GUI verification checklist for the new
info / profile / aux surface. Appendable to the v4 Phase 9 cumulative
recipe to catch metadata-vs-plates/objects/filaments interactions."
```

---

## Task 24: Update `docs/cli/status.md` (if present) with Phase 10 status block

**Files:**
- Modify: `docs/cli/status.md` (skip task if file does not exist)

- [ ] **Step 1: Check existence**

```powershell
Test-Path docs/cli/status.md
```

If `False`, skip this task entirely (status doc may have been removed in a later cleanup). Move on to Task 25.

- [ ] **Step 2: Append a Phase 10 block matching the existing format**

```markdown
## Phase 10 — Project tab editing (info / profile / aux)

**Status:** complete, NOT pushed.

**Verbs shipped:**
- `project info  {show,set,clear}` — title, description, license, copyright, cover (PNG-only)
- `project profile {show,set,clear}` — title, description, cover; user_id / user_name read-only
- `project aux {list,add,remove,export}` — pictures / bom / assembly-guide / others buckets

**Tests:** +40 cases (24 unit + 13 e2e + 3 roundtrip).
**Spec:** `docs/superpowers/specs/2026-05-20-orca-cli-project-tab-design.md`.
**Plan:** `docs/superpowers/plans/2026-05-20-orca-cli-project-tab.md`.
```

- [ ] **Step 3: Commit (if Step 2 ran)**

```powershell
git add docs/cli/status.md
git commit -m "docs(cli): Phase 10 status block"
```

---

## Task 25: Final verification — run the whole suite + count delta + cumulative recipe

**Files:** none (verification).

- [ ] **Step 1: Build everything clean**

```powershell
cmake --build build --config Release --target cli_tests orca-cli -- -m
```

Expected: both targets build cleanly. No warnings introduced (compare against Task 0's build output).

- [ ] **Step 2: Run the full suite in random order**

```powershell
& "build\tests\cli\Release\cli_tests.exe" --order rand --warn NoAssertions
```

Expected: `All tests passed` ending. Test-case count delta should be exactly **+40** (24 unit + 13 e2e + 3 roundtrip) versus Task 0's baseline; if it's off by more than ±2, investigate before shipping.

- [ ] **Step 3: Run the project-tab tag in isolation**

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][project-tab]" --order rand --warn NoAssertions
```

Expected: the ~40 new cases pass, no failures, no skips (other than the standard `if (ref_3mf().empty()) SUCCEED("Skipped")` for tests that depend on the in-tree reference fixtures).

- [ ] **Step 4: Run the Phase 10 manual smoke recipe**

Follow `docs/cli/manual-test.md` Phase 10 step-by-step. Open the resulting `$env:TEMP\orca-cli-phase10.3mf` in OrcaSlicer GUI. Verify the four GUI assertions listed in the recipe.

- [ ] **Step 5: Run the cumulative recipe**

In the same OrcaSlicer GUI session that just confirmed Phase 10, also confirm the prior phases (1-9) still render correctly in the Project tab — particularly that plates, objects, and filaments are unchanged. The new metadata should be additive, not replacing prior state.

- [ ] **Step 6: Record final test counts in a session memory note (or PR description)**

Update the project memory at `~/.claude/projects/.../memory/project_orca_cli_v?.md`:

- Ship status: complete, on `main`.
- Test count delta: from Task 0's baseline to Step 2's new total.
- GUI verification: PASS for Phase 10 standalone + cumulative.
- Push status: NOT pushed (per [[feedback-autonomous-execution]]).

This is the closeout. Subsequent sessions can find the status via memory recall instead of re-walking the plan.

- [ ] **Step 7: Final no-op commit (only if the cumulative recipe surfaced any tweak)**

If everything passed without changes, skip this. If you fixed something cosmetic in the docs or a test message during verification, commit it as `fix(cli): post-verification cleanup` and call out the change in the memory note.

---

## Spec coverage map

Every requirement in the spec maps to a task above. Quick cross-reference:

| Spec section | Implementing tasks |
|--------------|-------------------|
| § 1 (scope: info / profile / aux subset) | Tasks 5-7, 11, 13-16 |
| § 2 (command surface) | Tasks 18-20 |
| § 2.1 (cover semantics, refcount, name sanitization, --force) | Tasks 9-12, 14 |
| § 2.2 (stable JSON shape, key-set always present) | Tasks 18-20 (show/list emitters); roundtrip in Task 21 |
| § 2.3 (hyphen-flag vs underscore-JSON naming) | Task 4 (helpers), Task 20 (e2e regression) |
| § 3.1 (locus: project_tab_ops.{hpp,cpp} + commands/project_tab.{hpp,cpp}) | Tasks 1-3, 17 |
| § 3.2 (clone-and-mutate flow via run_mutation) | Tasks 18-20 (mutating verbs); Task 16 (export skips run_mutation per design) |
| § 3.3 (aux file plumbing via get_auxiliary_file_temp_path) | Tasks 13-16 |
| § 3.4 (PNG-only cover embed; magic bytes; canonical path) | Tasks 8-9 |
| § 3.5 (cover-clear refcount) | Task 10 |
| § 3.6 (read path: no guard, no rename) | Tasks 18-20 (show/list/export) |
| § 4 (per-verb data flow) | Tasks 5-20 (every op) |
| § 5.1 (exit-code mapping table) | Tasks 18-20 (MutationExceptionMap registrations) |
| § 5.2 (idempotency) | Tasks 7, 10, 14 (--force on identical bytes — covered by overwrite semantics) |
| § 5.3 (guard failures via run_mutation) | Tasks 18-20 (inherit from run_mutation) |
| § 5.4 (deterministic validation order) | Tasks 14, 20 (folder enum → file existence → load → name → collision → mutate → save) |
| § 5.5 (--help on each verb with example) | Tasks 18-20 (CLI11 add_option descriptions) |
| § 6.1 (unit tests, ~24 cases) | Tasks 4-16 |
| § 6.2 (e2e tests, ~13 cases) | Tasks 18-20 |
| § 6.3 (roundtrip tests, 3 cases) | Task 21 |
| § 6.4 (Phase 10 manual smoke) | Task 23 |
| § 6.5 (3 fixture files) | Task 22 |
| § 7 (out-of-scope: deferred follow-ups) | Not implemented (intentional) |
