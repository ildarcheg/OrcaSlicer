# orca-cli Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Address every HIGH and MEDIUM finding from the 2026-05-19 review of the orca-cli v2 code (`src/cli/**`, `tests/cli/**`) without regressing the 109-case / 66,046-assertion test suite or changing user-visible CLI behavior except for slight JSON-output formatting changes documented in Task 9.

**Architecture:** Refactor in 15 small, independently-committable tasks. Order is chosen so that each later task builds on the helpers/refactors produced by earlier tasks: Tasks 1-5 introduce small shared helpers; Tasks 6-8 generalize the command-layer envelope and tighten one parser; Tasks 9-10 replace hand-rolled JSON with `nlohmann::json`; Tasks 11-15 refactor and speed up the I/O hot path. After every task the full `cli_tests` suite must be green before committing.

**Tech Stack:** C++17, libslic3r, miniz (via libslic3r's `miniz_extension`), nlohmann::json (already a libslic3r transitive dependency), Catch2 v2 (per `tests/CLAUDE.md`), CMake/CTest, CLI11 (vendored).

**Build & test commands** (Windows, PowerShell):

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

For a faster iteration on a single suite, invoke the test binary directly with a tag filter:

```powershell
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli]" --order rand --warn NoAssertions
```

**Catch2 conventions to follow** (per `tests/CLAUDE.md`):
- No `Catch::Approx` — use `WithinAbs` / `WithinRel`.
- No `&&` / `||` inside `REQUIRE` — split into separate `REQUIRE` lines.
- Use `DYNAMIC_SECTION` (not `SECTION`) inside loops.
- New test cases use the existing `[orca-cli]` prefix tag plus a `[cleanup]` tag so they can be filtered.

**Source-file naming convention used below**:
- New shared helper headers go alongside the file they extend (`io.hpp`, `project_ops.hpp`, etc.) — no new top-level dirs.
- All new code stays inside `namespace orca_cli` (or `orca_cli::commands` for the per-subcommand TUs).

---

## File Structure Overview

Files modified or created across the plan (no file is created from scratch; every change is incremental):

| File | What changes |
|------|--------------|
| `src/cli/io.hpp` | Add `check_input_exists` (T1), `enumerate_zip_entry_names` + `extract_entry_to_memory` (T12). |
| `src/cli/io.cpp` | Add helper impls; rewrite `passthrough_missing_thumbnails` into 4 smaller functions (T13); switch carried-forward entries to `mz_zip_writer_add_from_zip_reader` (T14); adopt safer rename in `save_project` (T11). |
| `src/cli/project_ops.hpp` | Expose `find_object` (T2), `filament_slot_count` (T3). |
| `src/cli/project_ops.cpp` | Replace `read_printable_area_aabb` with libslic3r `BoundingBoxf` (T5). |
| `src/cli/invariants.cpp` | Use new name-only zip enumerator (T8/T12); call `plate_thumbnail_paths` (T4). |
| `src/cli/invariants.hpp` | (No public surface change expected.) |
| `src/cli/png_placeholder.hpp` | Add `plate_thumbnail_paths(int n_one_based)` (T4). |
| `src/cli/png_placeholder.cpp` | Implement `plate_thumbnail_paths`. |
| `src/cli/output.hpp` | Replace hand-rolled JSON surface with `nlohmann::json`-backed `print_ok` / `print_err`; add `emit_list_response` (T9, T10). |
| `src/cli/output.cpp` | Replace `escape_json` / `print_ok` / `print_err` impls (T9); implement `emit_list_response` (T10). |
| `src/cli/CMakeLists.txt` | Link `nlohmann_json::nlohmann_json` to `orca_cli_core` (T9). |
| `src/cli/commands/plate.cpp` | Drop local `check_input_exists` (T1); collapse `do_plate_remove` into the generalized `run_mutation` (T8); switch `do_plate_list` to `emit_list_response` (T10). |
| `src/cli/commands/object.cpp` | Drop local `check_input_exists` (T1); use `find_object` (T2); change `parse_vec3` return type (T6); introduce `AddObjectRawOpts` (T7); route via generalized `run_mutation` (T8); switch list to `emit_list_response` (T10). |
| `src/cli/commands/config.cpp` | Drop local `check_input_exists` (T1); use `find_object` (T2); route via generalized `run_mutation` (T8); switch list to `emit_list_response` (T10). |
| `src/cli/commands/inspect.cpp` | Drop local `check_input_exists` (T1); use `filament_slot_count` (T3); use `find_object` (T2); render JSON via `nlohmann::json` (T9). |
| `src/cli/commands/project_init.cpp` | Drop local `check_input_exists` if present (T1). |
| `tests/cli/unit/test_output.cpp` | Add focused tests for the new `nlohmann::json`-backed envelope (T9) and `emit_list_response` (T10). |
| `tests/cli/unit/test_project_ops.cpp` | Add tests for `find_object` (T2), `filament_slot_count` (T3), `BoundingBoxf` printable-area read (T5). |
| `tests/cli/unit/test_png_placeholder.cpp` | Add a test for `plate_thumbnail_paths` (T4). |
| `tests/cli/unit/test_invariants.cpp` | Update if the in-memory enumeration API surface changes (T12). |
| `tests/cli/e2e/test_plate.cpp` / `test_object.cpp` / `test_config.cpp` / `test_inspect.cpp` | Re-run; update goldens iff Task 9 changes the exact JSON key order (the JSON spec does not require a stable key order — tests should parse, not string-compare; if they string-compare, switch them to parse via `nlohmann::json::parse` and assert on fields). |

---

## Task 1 — M3: Centralize `check_input_exists`

**Why:** Four subcommand files (`plate.cpp`, `object.cpp`, `config.cpp`, `inspect.cpp`) each define an identical anonymous-namespace `check_input_exists` helper with drifting error strings ("input not found" vs "file not found" vs "template not found"). Pick one home and one message.

**Files:**
- Modify: `src/cli/io.hpp`
- Modify: `src/cli/io.cpp`
- Modify: `src/cli/commands/plate.cpp` (lines 23-32 — remove local copy)
- Modify: `src/cli/commands/object.cpp` (lines 32-39 — remove local copy)
- Modify: `src/cli/commands/config.cpp` (the local `check_input_exists` block)
- Modify: `src/cli/commands/inspect.cpp` (the local `check_input_exists` block)
- Modify: `src/cli/commands/project_init.cpp` (if it has one)
- Test: `tests/cli/unit/test_project_ops.cpp` (add a focused unit test)

- [ ] **Step 1: Write the failing test**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
#include "io.hpp"

TEST_CASE("check_input_exists returns ok for existing file", "[orca-cli][cleanup][T1]") {
    orca_cli::GlobalOpts g;
    g.json = false;
    const std::string ref = ORCA_CLI_REF_3MF;
    int rc = orca_cli::check_input_exists(g, ref);
    REQUIRE(rc == int(orca_cli::ExitCode::ok));
}

TEST_CASE("check_input_exists returns file_not_found for missing path", "[orca-cli][cleanup][T1]") {
    orca_cli::GlobalOpts g;
    g.json = false;
    int rc = orca_cli::check_input_exists(g, "C:/this/path/does/not/exist.3mf");
    REQUIRE(rc == int(orca_cli::ExitCode::file_not_found));
}
```

- [ ] **Step 2: Run test to verify it fails**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli][cleanup][T1]" --order rand --warn NoAssertions
```

Expected: compilation error (`check_input_exists` is not in `orca_cli` namespace yet).

- [ ] **Step 3: Add the public helper in `io.hpp`**

Insert after `resolve_save_target` in `src/cli/io.hpp`:

```cpp
// Common preflight used by every mutating and read-only subcommand:
// returns ExitCode::ok if `path` exists, else prints a uniform
// "input not found: <path>" err line and returns ExitCode::file_not_found.
int check_input_exists(const GlobalOpts& g, const std::string& path);
```

- [ ] **Step 4: Implement in `io.cpp`**

Add to `src/cli/io.cpp` (near the existing `resolve_save_target` impl, inside `namespace orca_cli`):

```cpp
int check_input_exists(const GlobalOpts& g, const std::string& path) {
    namespace fs = boost::filesystem;
    if (!fs::exists(path)) {
        print_err(g, ExitCode::file_not_found, "input not found: " + path);
        return int(ExitCode::file_not_found);
    }
    return int(ExitCode::ok);
}
```

Make sure `output.hpp` is included in `io.cpp` (for `print_err` / `ExitCode`).

- [ ] **Step 5: Delete the four local copies**

In `src/cli/commands/plate.cpp`, `object.cpp`, `config.cpp`, `inspect.cpp` (and `project_init.cpp` if present): delete the anonymous-namespace `int check_input_exists(const GlobalOpts& g, const std::string& path)` function. Every call site already uses the bare name `check_input_exists(...)`; via ADL it now resolves to the `orca_cli::check_input_exists` because each call site is already inside `namespace orca_cli::commands` (which transitively reaches `orca_cli`). If a TU fails to compile, add an explicit `orca_cli::check_input_exists(...)` qualification.

- [ ] **Step 6: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all 109+ tests PASS. If any test fails on the error string ("input not found" vs "file not found"), update the test to the new uniform string.

- [ ] **Step 7: Commit**

```powershell
git add src/cli/io.hpp src/cli/io.cpp src/cli/commands/plate.cpp src/cli/commands/object.cpp src/cli/commands/config.cpp src/cli/commands/inspect.cpp src/cli/commands/project_init.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "refactor(cli): centralize check_input_exists in io.hpp (M3)"
```

---

## Task 2 — M9: Centralize `find_object`

**Why:** Five places do a linear scan for a `ModelObject` by name (`project_ops.cpp::find_object_or_throw` at line 344, `commands/config.cpp::find_object` near line 70, plus inline loops in `set_object_filament`, `object_config_keys`, `remove_object`). Promote one helper.

**Files:**
- Modify: `src/cli/project_ops.hpp` (add public declarations)
- Modify: `src/cli/project_ops.cpp` (replace 5 lookup sites with the helpers)
- Modify: `src/cli/commands/config.cpp` (drop local `find_object`)
- Test: `tests/cli/unit/test_project_ops.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("find_object returns nullptr when missing", "[orca-cli][cleanup][T2]") {
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    REQUIRE(find_object(s, "__nope__") == nullptr);
}

TEST_CASE("find_object_or_throw throws on missing", "[orca-cli][cleanup][T2]") {
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    REQUIRE_THROWS_AS(find_object_or_throw(s, "__nope__"), std::out_of_range);
}
```

- [ ] **Step 2: Run test to verify it fails**

```powershell
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli][cleanup][T2]" --order rand --warn NoAssertions
```

Expected: compile error (`find_object` / `find_object_or_throw` are anonymous-namespace).

- [ ] **Step 3: Add public declarations to `project_ops.hpp`**

Just below the `ProjectState` struct, add:

```cpp
// Look up a ModelObject by exact name. Returns nullptr if no object
// matches; const overload provided for read-only inspection paths.
Slic3r::ModelObject*       find_object(ProjectState& s,       const std::string& name);
const Slic3r::ModelObject* find_object(const ProjectState& s, const std::string& name);

// Same as find_object, but throws std::out_of_range when missing — used
// by mutation paths whose contract is "fail loud" so the command catch
// chain can map to ExitCode::unknown_reference.
Slic3r::ModelObject& find_object_or_throw(ProjectState& s, const std::string& name);
```

- [ ] **Step 4: Implement in `project_ops.cpp`**

At the top of `project_ops.cpp` (just inside `namespace orca_cli`), add:

```cpp
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
```

Delete the existing anonymous-namespace `find_object_or_throw` near line 344. Replace its call sites (`set_object_filament`, `set_object_config`, `unset_object_config`, `object_config_keys`, `remove_object`) so they use the new free function; the call shape changes from `find_object_or_throw(s, name)` (pointer) to `find_object_or_throw(s, name)` (ref) — adapt callers accordingly (typically `auto& obj = find_object_or_throw(...);` then operate on `obj`, or `auto* objp = &find_object_or_throw(...);` if pointer arithmetic is needed).

- [ ] **Step 5: Drop the duplicate in `config.cpp`**

In `src/cli/commands/config.cpp`, delete the local anonymous-namespace `find_object` helper near line 70. Call sites that used it now resolve to `orca_cli::find_object` via the existing `using` / `namespace orca_cli::commands` nesting.

- [ ] **Step 6: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS.

- [ ] **Step 7: Commit**

```powershell
git add src/cli/project_ops.hpp src/cli/project_ops.cpp src/cli/commands/config.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "refactor(cli): centralize find_object in project_ops (M9)"
```

---

## Task 3 — M10: Move `filament_slot_count` into `project_ops`

**Why:** `inspect.cpp` defines its own `filament_slot_count` while `project_ops.cpp::filament_count_of` does the same job. Promote one.

**Files:**
- Modify: `src/cli/project_ops.hpp`
- Modify: `src/cli/project_ops.cpp` (rename/promote `filament_count_of`)
- Modify: `src/cli/commands/inspect.cpp` (drop local copy)
- Test: `tests/cli/unit/test_project_ops.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("filament_slot_count >= 1 on reference", "[orca-cli][cleanup][T3]") {
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    REQUIRE(filament_slot_count(*s.project_config) >= 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

```powershell
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli][cleanup][T3]" --order rand --warn NoAssertions
```

Expected: compile error (`filament_slot_count` undefined).

- [ ] **Step 3: Expose helper in `project_ops.hpp`**

Add (after `changed_project_keys`):

```cpp
// Number of filament slots configured in the project (the size of the
// filament_settings_id ConfigOptionStrings; min-clamped to 1).
int filament_slot_count(const Slic3r::DynamicPrintConfig& cfg);
```

- [ ] **Step 4: Implement in `project_ops.cpp`**

Promote the existing anonymous-namespace `filament_count_of` to file scope under the new name `filament_slot_count`:

```cpp
int filament_slot_count(const Slic3r::DynamicPrintConfig& cfg) {
    using namespace Slic3r;
    if (auto* fsid = cfg.option<ConfigOptionStrings>("filament_settings_id"))
        return std::max(1, int(fsid->values.size()));
    return 1;
}
```

Update internal callers of `filament_count_of` to use `filament_slot_count` instead; delete the anonymous-namespace alias.

- [ ] **Step 5: Drop local copy in `inspect.cpp`**

In `src/cli/commands/inspect.cpp`, delete the local `filament_slot_count` (or whatever the local helper is named) and call `orca_cli::filament_slot_count(*state.project_config)` directly.

- [ ] **Step 6: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS.

- [ ] **Step 7: Commit**

```powershell
git add src/cli/project_ops.hpp src/cli/project_ops.cpp src/cli/commands/inspect.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "refactor(cli): promote filament_slot_count to project_ops (M10)"
```

---

## Task 4 — M7: `plate_thumbnail_paths` helper

**Why:** `Metadata/plate_<n>.png`, `_small.png`, `_no_light_<n>.png`, `top_<n>.png`, `pick_<n>.png` are hardcoded in 5+ places (`io.cpp` lines 72, 156-157, 184; `invariants.cpp` near lines 112, 118; the `thumbnail_re` regex). One source of truth.

**Files:**
- Modify: `src/cli/png_placeholder.hpp`
- Modify: `src/cli/png_placeholder.cpp`
- Modify: `src/cli/io.cpp` (use helper in the synthesize loop near line 154-163; keep regex for orphan detection as-is)
- Modify: `src/cli/invariants.cpp` (use helper in `verify_plate_thumbnails`)
- Test: `tests/cli/unit/test_png_placeholder.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/cli/unit/test_png_placeholder.cpp`:

```cpp
TEST_CASE("plate_thumbnail_paths(1) returns canonical names", "[orca-cli][cleanup][T4]") {
    auto p = orca_cli::plate_thumbnail_paths(1);
    REQUIRE(p.mid       == "Metadata/plate_1.png");
    REQUIRE(p.small     == "Metadata/plate_1_small.png");
    REQUIRE(p.no_light  == "Metadata/plate_no_light_1.png");
    REQUIRE(p.top       == "Metadata/top_1.png");
    REQUIRE(p.pick      == "Metadata/pick_1.png");
}
```

- [ ] **Step 2: Run test to verify it fails**

```powershell
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli][cleanup][T4]" --order rand --warn NoAssertions
```

Expected: compile error.

- [ ] **Step 3: Declare in `png_placeholder.hpp`**

Add:

```cpp
struct PlateThumbnailPaths {
    std::string mid;       // Metadata/plate_<n>.png
    std::string small;     // Metadata/plate_<n>_small.png
    std::string no_light;  // Metadata/plate_no_light_<n>.png
    std::string top;       // Metadata/top_<n>.png
    std::string pick;      // Metadata/pick_<n>.png
};

// Returns the canonical plate-thumbnail entry names for plate index
// `n_one_based` (which is 1-based, matching on-disk PNG naming).
PlateThumbnailPaths plate_thumbnail_paths(int n_one_based);
```

- [ ] **Step 4: Implement in `png_placeholder.cpp`**

```cpp
PlateThumbnailPaths plate_thumbnail_paths(int n) {
    const std::string s = std::to_string(n);
    return PlateThumbnailPaths{
        "Metadata/plate_" + s + ".png",
        "Metadata/plate_" + s + "_small.png",
        "Metadata/plate_no_light_" + s + ".png",
        "Metadata/top_" + s + ".png",
        "Metadata/pick_" + s + ".png",
    };
}
```

- [ ] **Step 5: Use the helper in `io.cpp`**

In `passthrough_missing_thumbnails`, replace the literal-string block (~lines 154-163):

```cpp
    std::vector<std::string> to_synthesize;
    for (size_t i = 1; i <= s.plates.size(); ++i) {
        const auto t = plate_thumbnail_paths(int(i));
        for (const std::string& name : {t.mid, t.small}) {
            if (target_entries.count(name))     continue;
            if (planned_copy_names.count(name)) continue;
            to_synthesize.push_back(name);
        }
    }
```

- [ ] **Step 6: Use the helper in `invariants.cpp`**

In `verify_plate_thumbnails`, replace the inline `"Metadata/plate_" + N + ".png"` and `"_small"` literals with `plate_thumbnail_paths(int(i)).mid` / `.small`.

(The `thumbnail_re` regex in `io.cpp` stays as-is — it scans for `_no_light_`, `top_`, `pick_` and orphans across all plates, which is shaped differently from the synthesis loop.)

- [ ] **Step 7: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS.

- [ ] **Step 8: Commit**

```powershell
git add src/cli/png_placeholder.hpp src/cli/png_placeholder.cpp src/cli/io.cpp src/cli/invariants.cpp tests/cli/unit/test_png_placeholder.cpp
git commit -m "refactor(cli): plate_thumbnail_paths helper for canonical PNG names (M7)"
```

---

## Task 5 — H4: Replace bbox loop with `BoundingBoxf`

**Why:** `read_printable_area_aabb` in `project_ops.cpp:89-108` hand-rolls a min/max loop over `pa->values` (a `std::vector<Vec2d>`). libslic3r's `BoundingBoxf(const std::vector<Vec2d>&)` ctor does the same thing.

**Files:**
- Modify: `src/cli/project_ops.cpp`
- Test: `tests/cli/unit/test_project_ops.cpp` (reference 3mf already exercises this via `add_object`; add one targeted test)

- [ ] **Step 1: Write the failing test (regression pin)**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("printable_area read produces same bed extent before/after refactor",
          "[orca-cli][cleanup][T5]") {
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    // Add object to plate 1 of the reference; the placement math reads
    // printable_area internally. The reference is a 4-point 256x256 bed.
    AddObjectParams p;
    p.plate_name = s.plates.front()->plate_name;
    p.stl_path   = std::string(ORCA_CLI_STL_DIR) + "/cube.stl";
    p.object_name = "cube_t5";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE(s.model->objects.back()->name == "cube_t5");
}
```

- [ ] **Step 2: Run test to verify baseline passes**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli][cleanup][T5]" --order rand --warn NoAssertions
```

Expected: PASS (baseline).

- [ ] **Step 3: Replace the loop**

In `src/cli/project_ops.cpp:85-108`, replace the entire `read_printable_area_aabb` function body with:

```cpp
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
```

Add `#include <libslic3r/BoundingBox.hpp>` if it's not already pulled in transitively (likely it is, via `PrintConfig.hpp`).

- [ ] **Step 4: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS — placement output must be byte-identical.

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "refactor(cli): use BoundingBoxf for printable_area read (H4)"
```

---

## Task 6 — M5: `parse_vec3` returns `ParsedVec3`

**Why:** `parse_vec3` returns `std::optional<Vec3d>`, then the caller re-runs `std::count(... ',')` to enforce arity per flag (`--rotate` requires 3 components, `--scale` accepts 1 or 3, `--translate` accepts 2 or 3). The arity check belongs in the parser.

**Files:**
- Modify: `src/cli/commands/object.cpp`
- Test: `tests/cli/unit/test_project_ops.cpp` (or a new dedicated test in `unit/`)

- [ ] **Step 1: Write the failing test**

Add a new file `tests/cli/unit/test_object_parse_vec3.cpp`:

```cpp
#include <catch2/catch.hpp>
#include "commands/object_parse_vec3.hpp"

using namespace orca_cli::commands;

TEST_CASE("parse_vec3 reports component_count", "[orca-cli][cleanup][T6]") {
    SECTION("1 component") {
        auto r = parse_vec3("2");
        REQUIRE(r.has_value());
        REQUIRE(r->component_count == 1);
        REQUIRE(r->values.x() == 2.0);
        REQUIRE(r->values.y() == 2.0);
        REQUIRE(r->values.z() == 2.0);
    }
    SECTION("2 components") {
        auto r = parse_vec3("60,60");
        REQUIRE(r.has_value());
        REQUIRE(r->component_count == 2);
        REQUIRE(r->values.x() == 60.0);
        REQUIRE(r->values.z() == 0.0);
    }
    SECTION("3 components") {
        auto r = parse_vec3("1,2,3");
        REQUIRE(r.has_value());
        REQUIRE(r->component_count == 3);
        REQUIRE(r->values.z() == 3.0);
    }
    SECTION("empty token rejected") {
        REQUIRE_FALSE(parse_vec3("60,,60").has_value());
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

```powershell
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli][cleanup][T6]" --order rand --warn NoAssertions
```

Expected: compile error — the new header doesn't exist yet.

- [ ] **Step 3: Extract the parser to a header**

Create `src/cli/commands/object_parse_vec3.hpp`:

```cpp
#pragma once
#include <libslic3r/Point.hpp>
#include <optional>
#include <string>

namespace orca_cli::commands {

struct ParsedVec3 {
    Slic3r::Vec3d values;        // broadcast/extended to 3 components
    int           component_count; // 1, 2, or 3 — the original input arity
};

// Parse "s" / "x,y" / "x,y,z". 1-component -> {s,s,s}; 2-component -> {x,y,0}.
// Returns nullopt if any token is empty, non-finite, or not parseable;
// also nullopt for >3 components.
std::optional<ParsedVec3> parse_vec3(const std::string& s);

} // namespace orca_cli::commands
```

- [ ] **Step 4: Move the implementation into a new .cpp**

Create `src/cli/commands/object_parse_vec3.cpp`:

```cpp
#include "object_parse_vec3.hpp"
#include <cmath>
#include <vector>

namespace orca_cli::commands {

std::optional<ParsedVec3> parse_vec3(const std::string& s) {
    if (s.empty()) return std::nullopt;
    std::vector<double> nums;
    std::string token;
    auto trim = [](std::string& t) {
        size_t a = t.find_first_not_of(" \t");
        size_t b = t.find_last_not_of(" \t");
        if (a == std::string::npos) t.clear();
        else t = t.substr(a, b - a + 1);
    };
    auto flush = [&]() -> bool {
        trim(token);
        if (token.empty()) return false;
        try {
            size_t consumed = 0;
            double v = std::stod(token, &consumed);
            if (consumed != token.size()) return false;
            if (!std::isfinite(v))        return false;
            nums.push_back(v);
        } catch (...) { return false; }
        token.clear();
        return true;
    };
    for (char ch : s) {
        if (ch == ',') { if (!flush()) return std::nullopt; }
        else           { token.push_back(ch); }
    }
    if (!flush()) return std::nullopt;
    if (nums.size() == 1) return ParsedVec3{Slic3r::Vec3d(nums[0], nums[0], nums[0]), 1};
    if (nums.size() == 2) return ParsedVec3{Slic3r::Vec3d(nums[0], nums[1], 0.0),     2};
    if (nums.size() == 3) return ParsedVec3{Slic3r::Vec3d(nums[0], nums[1], nums[2]), 3};
    return std::nullopt;
}

} // namespace orca_cli::commands
```

Add `commands/object_parse_vec3.cpp` to the `orca_cli_core` source list in `src/cli/CMakeLists.txt`.

- [ ] **Step 5: Rewrite the call sites in `commands/object.cpp`**

Delete the existing `parse_vec3` anonymous-namespace definition. Update each caller to use the new return shape and enforce arity by inspecting `component_count`:

```cpp
#include "object_parse_vec3.hpp"

// --translate: 2 or 3 components.
std::optional<Slic3r::Vec3d> translate;
if (!translate_str.empty()) {
    auto r = parse_vec3(translate_str);
    if (!r || (r->component_count != 2 && r->component_count != 3)) {
        print_err(g, ExitCode::usage_error,
                  "invalid --translate value '" + translate_str +
                  "' (expected x,y or x,y,z)");
        return int(ExitCode::usage_error);
    }
    translate = r->values;
}

// --rotate: exactly 3 components.
std::optional<Slic3r::Vec3d> rotate;
if (!rotate_str.empty()) {
    auto r = parse_vec3(rotate_str);
    if (!r || r->component_count != 3) {
        print_err(g, ExitCode::usage_error,
                  "invalid --rotate value '" + rotate_str +
                  "' (expected ax,ay,az in radians)");
        return int(ExitCode::usage_error);
    }
    rotate = r->values;
}

// --scale: scalar (1 component) or 3 components.
std::optional<Slic3r::Vec3d> scale;
if (!scale_str.empty()) {
    auto r = parse_vec3(scale_str);
    if (!r || (r->component_count != 1 && r->component_count != 3)) {
        print_err(g, ExitCode::usage_error,
                  "invalid --scale value '" + scale_str +
                  "' (expected s or sx,sy,sz)");
        return int(ExitCode::usage_error);
    }
    scale = r->values;
}
```

Delete the `int commas = std::count(...)` blocks (lines ~131-141 and ~149-155 in the current `object.cpp`).

- [ ] **Step 6: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS — including the P4 transform tests (`--translate`, `--rotate`, `--scale`, bad-value `usage_error`).

- [ ] **Step 7: Commit**

```powershell
git add src/cli/commands/object_parse_vec3.hpp src/cli/commands/object_parse_vec3.cpp src/cli/commands/object.cpp src/cli/CMakeLists.txt tests/cli/unit/test_object_parse_vec3.cpp
git commit -m "refactor(cli): parse_vec3 returns ParsedVec3; arity check moves into parser (M5)"
```

---

## Task 7 — M4: `AddObjectRawOpts` struct for `do_object_add`

**Why:** `do_object_add` has 10 positional parameters (object.cpp:89-102) — high parallel-mistake risk between callback site and signature.

**Files:**
- Modify: `src/cli/commands/object.cpp`

- [ ] **Step 1: Define the struct (no test needed — pure refactor over compile-checked code)**

In `src/cli/commands/object.cpp`, near the top of the anonymous namespace:

```cpp
struct AddObjectRawOpts {
    std::string file;
    std::string plate;
    std::string stl;
    int         count           = 1;
    std::string name;
    std::string translate_str;
    std::string rotate_str;
    std::string scale_str;
    int         filament_slot   = 0; // 0 = unset
};
```

- [ ] **Step 2: Change the function signature**

Replace `int do_object_add(const GlobalOpts& g, const std::string& file, ... int filament_slot)` with:

```cpp
int do_object_add(const GlobalOpts& g, const AddObjectRawOpts& o)
```

Replace every parameter reference inside the body with `o.file`, `o.plate`, `o.stl`, etc.

- [ ] **Step 3: Update the registration site**

In `register_object_subcmd`, the `add` callback that previously read individual statics now constructs an `AddObjectRawOpts` from the statics and calls `do_object_add(g, opts)`. Group the related statics under one local struct or keep them as statics — either works; the change is just the call signature.

- [ ] **Step 4: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS.

- [ ] **Step 5: Commit**

```powershell
git add src/cli/commands/object.cpp
git commit -m "refactor(cli): AddObjectRawOpts struct for do_object_add (M4)"
```

---

## Task 8 — H1: Generalize `run_mutation` across every mutating command

**Why:** `commands/plate.cpp::run_mutation` extracted the "load -> mutate -> save -> map exception -> print_ok" envelope, but only `plate add` and `plate rename` use it. `do_plate_remove`, `do_object_add`, `do_object_set_filament`, `do_object_remove`, `do_config_set`, `do_config_unset` each re-implement the same try/catch ladder — and have already drifted (e.g. `std::invalid_argument` maps to `invalid_state` in plate remove vs `duplicate_name` in object add).

**Approach:** Lift `run_mutation` out of `plate.cpp` into a shared `commands/mutation_runner.hpp` so it's reachable from every subcommand, and give it an explicit exception-handler table (a `std::vector<std::function<int(const std::exception&)>>` or a small `ExceptionMap` with typed predicates). Each subcommand passes the table it needs.

**Files:**
- Create: `src/cli/commands/mutation_runner.hpp`
- Create: `src/cli/commands/mutation_runner.cpp`
- Modify: `src/cli/CMakeLists.txt` (add the new .cpp to `orca_cli_core`)
- Modify: `src/cli/commands/plate.cpp` (delete local `run_mutation` / `map_mutation_exception` / `do_plate_remove`'s body; replace with `run_mutation` calls)
- Modify: `src/cli/commands/object.cpp` (route `do_object_add` / `do_object_set_filament` / `do_object_remove` through `run_mutation`)
- Modify: `src/cli/commands/config.cpp` (route `do_config_set` / `do_config_unset` through `run_mutation`)
- Test: `tests/cli/unit/test_mutation_runner.cpp` (new)

- [ ] **Step 1: Write the failing test**

Create `tests/cli/unit/test_mutation_runner.cpp`:

```cpp
#include <catch2/catch.hpp>
#include "commands/mutation_runner.hpp"

using namespace orca_cli;
using namespace orca_cli::commands;

namespace {
struct DummyError : std::runtime_error { using std::runtime_error::runtime_error; };
}

TEST_CASE("MutationExceptionMap dispatches custom exceptions first",
          "[orca-cli][cleanup][T8]") {
    GlobalOpts g; g.json = false;
    MutationExceptionMap em;
    em.on<DummyError>(ExitCode::bad_config);
    int rc = em.handle(g, DummyError("boom"));
    REQUIRE(rc == int(ExitCode::bad_config));
}

TEST_CASE("MutationExceptionMap default mapping for invalid_argument",
          "[orca-cli][cleanup][T8]") {
    GlobalOpts g; g.json = false;
    MutationExceptionMap em;
    em.set_default_invalid_argument(ExitCode::duplicate_name);
    int rc = em.handle(g, std::invalid_argument("dup"));
    REQUIRE(rc == int(ExitCode::duplicate_name));
}
```

- [ ] **Step 2: Run test to verify it fails**

```powershell
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli][cleanup][T8]" --order rand --warn NoAssertions
```

Expected: compile error (`mutation_runner.hpp` not yet created).

- [ ] **Step 3: Create `mutation_runner.hpp`**

```cpp
#pragma once
#include "../globals.hpp"
#include "../output.hpp"
#include "../invariants.hpp"
#include "../io.hpp"
#include "../project_ops.hpp"
#include <exception>
#include <functional>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace orca_cli::commands {

// Maps caught exceptions to ExitCodes. Custom mappings (registered via on<T>())
// are tried first in registration order; the std::invalid_argument /
// std::out_of_range defaults fire only if no custom mapping matched.
//
// Why a small table type rather than a chain of catch clauses: the previous
// approach grew six independent try/catch ladders that drifted apart (e.g.
// invalid_argument -> invalid_state in plate remove vs duplicate_name in
// object add). Centralizing the mapping keeps the exit-code contract in one
// place per command, defined as data instead of nested control flow.
class MutationExceptionMap {
public:
    template <typename T>
    MutationExceptionMap& on(ExitCode code) {
        m_handlers.push_back({
            [](const std::exception& e) { return dynamic_cast<const T*>(&e) != nullptr; },
            code,
        });
        return *this;
    }
    MutationExceptionMap& set_default_invalid_argument(ExitCode c) {
        m_invalid_argument = c; return *this;
    }
    MutationExceptionMap& set_default_out_of_range(ExitCode c) {
        m_out_of_range = c; return *this;
    }
    int handle(const GlobalOpts& g, const std::exception& e) const {
        for (const auto& h : m_handlers) {
            if (h.predicate(e)) {
                print_err(g, h.code, e.what());
                return int(h.code);
            }
        }
        if (dynamic_cast<const std::invalid_argument*>(&e)) {
            print_err(g, m_invalid_argument, e.what());
            return int(m_invalid_argument);
        }
        if (dynamic_cast<const std::out_of_range*>(&e)) {
            print_err(g, m_out_of_range, e.what());
            return int(m_out_of_range);
        }
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
private:
    struct Handler {
        std::function<bool(const std::exception&)> predicate;
        ExitCode                                   code;
    };
    std::vector<Handler> m_handlers;
    ExitCode             m_invalid_argument = ExitCode::duplicate_name;
    ExitCode             m_out_of_range     = ExitCode::unknown_reference;
};

// Generic load -> mutate -> save runner. The mutate lambda gets a mutable
// ProjectState&; any exception it raises (other than InvariantViolation,
// which is handled here) is routed through `em` for exit-code mapping.
template <typename Mutator>
int run_mutation(const GlobalOpts&           g,
                 const std::string&          input,
                 const std::string&          ok_message,
                 const MutationExceptionMap& em,
                 Mutator&&                   mutate)
{
    if (int rc = check_input_exists(g, input); rc != int(ExitCode::ok))
        return rc;
    const std::string out = resolve_save_target(g, input);
    try {
        auto state = load_project(input);
        mutate(state);
        save_project(state, out);
    } catch (const InvariantViolation& e) {
        print_err(g, ExitCode::invariant_violation, e.what());
        return int(ExitCode::invariant_violation);
    } catch (const std::exception& e) {
        return em.handle(g, e);
    }
    print_ok(g, ok_message);
    return int(ExitCode::ok);
}

} // namespace orca_cli::commands
```

(No `.cpp` is strictly needed since the template + class are header-only. If link issues arise, factor `MutationExceptionMap::handle` into a `.cpp`.)

- [ ] **Step 4: Update `commands/plate.cpp`**

Delete the local `run_mutation` and `map_mutation_exception` definitions (lines 34-84 in current file). Replace `do_plate_remove` with:

```cpp
int do_plate_remove(const GlobalOpts& g, const std::string& input, const std::string& name) {
    MutationExceptionMap em;
    em.set_default_invalid_argument(ExitCode::invalid_state)
      .set_default_out_of_range(ExitCode::unknown_reference);
    return run_mutation(g, input, "removed plate '" + name + "'", em,
        [&name](ProjectState& s) {
            if (s.plates.size() <= 1)
                throw std::invalid_argument("cannot remove the only plate");
            remove_plate(s, name);
        });
}
```

Replace the existing `plate add` / `plate rename` callbacks to construct a default-constructed `MutationExceptionMap` (which gives them the same `invalid_argument -> duplicate_name`, `out_of_range -> unknown_reference` mapping they had).

- [ ] **Step 5: Update `commands/object.cpp`**

Replace each of `do_object_add`, `do_object_set_filament`, `do_object_remove` with the equivalent `run_mutation` call. `do_object_add` example:

```cpp
int do_object_add(const GlobalOpts& g, const AddObjectRawOpts& o) {
    if (int rc = check_input_exists(g, o.file); rc != int(ExitCode::ok)) return rc;
    if (!fs::exists(o.stl)) {
        print_err(g, ExitCode::file_not_found, "stl not found: " + o.stl);
        return int(ExitCode::file_not_found);
    }
    // ... parse transform flags up-front (Task 6) ...

    MutationExceptionMap em;
    em.on<PlacementFailure>(ExitCode::placement_failure);   // before out_of_range
    // invalid_argument default -> duplicate_name (unchanged)
    // out_of_range default     -> unknown_reference (unchanged)

    return run_mutation(g, o.file,
        "added object from '" + o.stl + "' to plate '" + o.plate + "'", em,
        [&](ProjectState& s) {
            AddObjectParams p;
            p.plate_name  = o.plate;
            p.stl_path    = o.stl;
            p.object_name = o.name;
            p.count       = o.count;
            p.translate   = translate;
            p.rotate      = rotate;
            p.scale       = scale;
            if (o.filament_slot != 0) p.filament_slot = o.filament_slot;
            add_object(s, p);
        });
}
```

`do_object_set_filament` and `do_object_remove` get default-constructed `MutationExceptionMap`s (their existing behavior already matches the defaults).

- [ ] **Step 6: Update `commands/config.cpp`**

Replace `do_config_set` / `do_config_unset` similarly. Their exception map needs `on<BadConfigError>(ExitCode::bad_config)` registered BEFORE the `out_of_range` default — and **before** the generic `std::exception` fallback, which `MutationExceptionMap::handle` already ensures because `BadConfigError` is a `std::runtime_error` (not a `std::invalid_argument`/`std::out_of_range`).

```cpp
MutationExceptionMap em;
em.on<BadConfigError>(ExitCode::bad_config);
// out_of_range default -> unknown_reference: keeps unknown-object semantics
return run_mutation(g, file, "set " + key + " = " + value, em,
    [&](ProjectState& s) {
        if (object_name.empty()) set_project_config(s, key, value);
        else                     set_object_config(s, object_name, key, value);
    });
```

- [ ] **Step 7: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all 109+ tests PASS, including the precise exit-code expectations (plate remove of last plate -> 7 `invalid_state`; object add off-bed -> 9 `placement_failure`; config set with unknown key -> 4 `bad_config`).

- [ ] **Step 8: Commit**

```powershell
git add src/cli/commands/mutation_runner.hpp src/cli/commands/plate.cpp src/cli/commands/object.cpp src/cli/commands/config.cpp src/cli/CMakeLists.txt tests/cli/unit/test_mutation_runner.cpp
git commit -m "refactor(cli): unify mutation envelope behind MutationExceptionMap + run_mutation (H1)"
```

---

## Task 9 — M1: Replace hand-rolled JSON with `nlohmann::json`

**Why:** `escape_json` is called inconsistently (`output.cpp` always; `plate.cpp` / `object.cpp` / `config.cpp` / `inspect.cpp` sometimes — anyone adding a future field that bypasses `escape_json` corrupts output). `nlohmann::json` is already a libslic3r dependency (used in `Print.cpp`, `AppConfig.cpp`, `ProjectTask.cpp`, `ThumbnailData.hpp`).

**Files:**
- Modify: `src/cli/output.hpp`
- Modify: `src/cli/output.cpp`
- Modify: `src/cli/CMakeLists.txt`
- Modify: `src/cli/commands/plate.cpp` (JSON list emission)
- Modify: `src/cli/commands/object.cpp` (JSON list emission)
- Modify: `src/cli/commands/config.cpp` (JSON list emission)
- Modify: `src/cli/commands/inspect.cpp` (JSON `data` object construction)
- Test: `tests/cli/unit/test_output.cpp`

**Compatibility note:** `nlohmann::json::dump()` may emit object keys in insertion order (it does, by default). Existing e2e tests that compare JSON output character-by-character must be switched to parse-and-assert instead. Inspect every `Contains("...")` / equality match in `test_plate.cpp` / `test_object.cpp` / `test_config.cpp` / `test_inspect.cpp` and update to use `nlohmann::json::parse(stdout_text)` then field checks.

- [ ] **Step 1: Write the failing test**

Append to `tests/cli/unit/test_output.cpp`:

```cpp
#include <nlohmann/json.hpp>

TEST_CASE("print_ok JSON is parseable and well-formed", "[orca-cli][cleanup][T9]") {
    // Capture stdout via freopen on Windows is finicky; we'll exercise the
    // helper used internally instead. (If output_ok_json doesn't exist yet,
    // this test drives its creation.)
    using namespace orca_cli;
    nlohmann::json data;
    data["plates"] = nlohmann::json::array();
    data["plates"].push_back({{"index", 1}, {"name", "P\"weird\""}});

    GlobalOpts g; g.json = true;
    // build_ok_envelope must be available as a unit-testable helper:
    std::string body = build_ok_envelope("listed 1 plates", data);

    auto j = nlohmann::json::parse(body);
    REQUIRE(j["status"] == "ok");
    REQUIRE(j["code"]   == "ok");
    REQUIRE(j["data"]["plates"][0]["name"] == "P\"weird\"");
}
```

- [ ] **Step 2: Run test to verify it fails**

```powershell
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli][cleanup][T9]" --order rand --warn NoAssertions
```

Expected: compile error (`build_ok_envelope` undefined).

- [ ] **Step 3: Rewrite `output.hpp`**

Replace the old surface with:

```cpp
#pragma once
#include "globals.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

namespace orca_cli {

enum class ExitCode { /* unchanged */ };
const char* code_name(ExitCode c);

// Build (do not print) the JSON ok envelope. Exposed for unit tests.
std::string build_ok_envelope(std::string_view message, const nlohmann::json& data);
std::string build_err_envelope(ExitCode code, std::string_view message);

// Plain-text "ok: <message>\n" or err line. Both also handle the JSON
// branch internally so callers can stay uniform.
void print_ok(const GlobalOpts& opts, std::string_view message);
void print_ok(const GlobalOpts& opts, std::string_view message, const nlohmann::json& data);
void print_err(const GlobalOpts& opts, ExitCode code, std::string_view message);

} // namespace orca_cli
```

Delete the `escape_json` declaration.

- [ ] **Step 4: Rewrite `output.cpp`**

```cpp
#include "output.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>

namespace orca_cli {

const char* code_name(ExitCode c) { /* unchanged */ }

std::string build_ok_envelope(std::string_view message, const nlohmann::json& data) {
    nlohmann::json j;
    j["status"]  = "ok";
    j["code"]    = "ok";
    j["message"] = std::string(message);
    if (!data.is_null() && !data.empty()) j["data"] = data;
    return j.dump();
}

std::string build_err_envelope(ExitCode code, std::string_view message) {
    nlohmann::json j;
    j["status"]  = "err";
    j["code"]    = code_name(code);
    j["message"] = std::string(message);
    return j.dump();
}

void print_ok(const GlobalOpts& opts, std::string_view message) {
    print_ok(opts, message, nlohmann::json::object());
}

void print_ok(const GlobalOpts& opts, std::string_view message, const nlohmann::json& data) {
    if (opts.json) {
        std::string body = build_ok_envelope(message, data);
        body.push_back('\n');
        std::fputs(body.c_str(), stdout);
    } else {
        std::fputs("ok: ", stdout);
        std::fwrite(message.data(), 1, message.size(), stdout);
        std::fputc('\n', stdout);
    }
    std::fflush(stdout);
}

void print_err(const GlobalOpts& opts, ExitCode code, std::string_view message) {
    if (opts.json) {
        std::string body = build_err_envelope(code, message);
        body.push_back('\n');
        std::fputs(body.c_str(), stdout);
    } else {
        std::fputs("err: ", stderr);
        std::fputs(code_name(code), stderr);
        std::fputs(": ", stderr);
        std::fwrite(message.data(), 1, message.size(), stderr);
        std::fputc('\n', stderr);
    }
    std::fflush(stdout);
    std::fflush(stderr);
}

} // namespace orca_cli
```

Delete the old `escape_json` definition.

- [ ] **Step 5: Wire nlohmann::json to the CLI target**

In `src/cli/CMakeLists.txt`, after the existing `target_link_libraries(orca_cli_core ...)`:

```cmake
target_link_libraries(orca_cli_core PUBLIC nlohmann_json::nlohmann_json)
```

(If the target name differs in this repo's vendored config, use whatever libslic3r itself links against — search `find_package(nlohmann_json` in the top-level `CMakeLists.txt` to confirm the imported target name.)

- [ ] **Step 6: Convert JSON emitters in the four subcommands**

`commands/plate.cpp` — replace the hand-rolled `plates_json` construction in `do_plate_list` with:

```cpp
nlohmann::json data;
auto& arr = data["plates"] = nlohmann::json::array();
for (size_t i = 0; i < state.plates.size(); ++i) {
    arr.push_back({
        {"index", int(i + 1)},
        {"name",  state.plates[i]->plate_name},
        {"object_count", int(state.plates[i]->objects_and_instances.size())},
    });
}
print_ok(g, "listed " + std::to_string(state.plates.size()) + " plates", data);
```

Apply analogous rewrites in `commands/object.cpp::do_object_list`, `commands/config.cpp::do_config_list`, and `commands/inspect.cpp::do_inspect` (the entire `data` object — `plate_count`, `filament_count`, `plates`, `project_changed`, `objects` — is built as nlohmann::json then handed to `print_ok`).

- [ ] **Step 7: Switch any e2e tests that string-compared JSON to parse-and-assert**

Identify the impacted tests with:

```powershell
Select-String -Path "tests\cli\e2e\*.cpp" -Pattern "ContainsSubstring|escape_json|""plates"":\["
```

For every test that asserts JSON via substring, switch to:

```cpp
auto j = nlohmann::json::parse(stdout_text);
REQUIRE(j["status"] == "ok");
REQUIRE(j["data"]["plates"].size() == expected_count);
```

- [ ] **Step 8: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS.

- [ ] **Step 9: Commit**

```powershell
git add src/cli/output.hpp src/cli/output.cpp src/cli/CMakeLists.txt src/cli/commands/plate.cpp src/cli/commands/object.cpp src/cli/commands/config.cpp src/cli/commands/inspect.cpp tests/cli/unit/test_output.cpp tests/cli/e2e/test_plate.cpp tests/cli/e2e/test_object.cpp tests/cli/e2e/test_config.cpp tests/cli/e2e/test_inspect.cpp
git commit -m "refactor(cli): use nlohmann::json for all CLI output; drop escape_json (M1)"
```

---

## Task 10 — M2: Consolidate `do_*_list` emitters

**Why:** `do_plate_list`, `do_object_list`, `do_config_list` all share the same `if (g.json) { build "..." JSON } else { fputs lines }` skeleton. After Task 9 the JSON branch is structurally identical; the human branch differs only in the per-row formatter.

**Files:**
- Modify: `src/cli/output.hpp`
- Modify: `src/cli/output.cpp`
- Modify: `src/cli/commands/plate.cpp`
- Modify: `src/cli/commands/object.cpp`
- Modify: `src/cli/commands/config.cpp`
- Test: `tests/cli/unit/test_output.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/cli/unit/test_output.cpp`:

```cpp
TEST_CASE("emit_list_response builds JSON envelope when json=true",
          "[orca-cli][cleanup][T10]") {
    using namespace orca_cli;
    struct Row { int idx; std::string name; };
    std::vector<Row> rows = {{1, "First"}, {2, "Second"}};

    nlohmann::json data;
    auto& arr = data["items"] = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back({{"index", r.idx}, {"name", r.name}});

    GlobalOpts g; g.json = true;
    std::string body = build_ok_envelope("listed 2 items", data);
    auto j = nlohmann::json::parse(body);
    REQUIRE(j["data"]["items"].size() == 2);
    REQUIRE(j["data"]["items"][1]["name"] == "Second");
}
```

(This test mirrors the API shape rather than calling `emit_list_response` directly; an integration check via the e2e tests in step 4 is the real coverage.)

- [ ] **Step 2: Add `emit_list_response` in `output.hpp` (optional helper)**

```cpp
// Convenience for "ls"-style subcommands: builds a JSON `data[list_name]`
// array via `to_json(row)` AND writes a human-readable line per row via
// `to_line(row)`. Selects the branch via opts.json.
template <typename Row, typename ToJson, typename ToLine>
void emit_list_response(const GlobalOpts& opts,
                       std::string_view  list_name,
                       std::string_view  summary,
                       const std::vector<Row>& rows,
                       ToJson&& to_json,
                       ToLine&& to_line)
{
    if (opts.json) {
        nlohmann::json data;
        auto& arr = data[std::string(list_name)] = nlohmann::json::array();
        for (const auto& r : rows) arr.push_back(to_json(r));
        print_ok(opts, summary, data);
    } else {
        for (const auto& r : rows) {
            std::string line = to_line(r);
            if (line.empty() || line.back() != '\n') line.push_back('\n');
            std::fputs(line.c_str(), stdout);
        }
        std::fflush(stdout);
    }
}
```

(This is a header-only template; include `<nlohmann/json.hpp>` in `output.hpp` for this. If header bloat becomes a concern, leave it as `inline` in `output.hpp`.)

- [ ] **Step 3: Convert `do_plate_list`**

```cpp
int do_plate_list(const GlobalOpts& g, const std::string& input) {
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error, "plate list does not accept --output");
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, input); rc != int(ExitCode::ok)) return rc;

    ProjectState state;
    try { state = load_project(input); }
    catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }

    struct Row { int index; std::string name; int obj_count; };
    std::vector<Row> rows;
    rows.reserve(state.plates.size());
    for (size_t i = 0; i < state.plates.size(); ++i) {
        rows.push_back({int(i + 1), state.plates[i]->plate_name,
                        int(state.plates[i]->objects_and_instances.size())});
    }
    emit_list_response(g, "plates",
        "listed " + std::to_string(rows.size()) + " plates", rows,
        [](const Row& r) { return nlohmann::json{{"index", r.index},
                                                  {"name",  r.name},
                                                  {"object_count", r.obj_count}}; },
        [](const Row& r) {
            return "plate " + std::to_string(r.index) + ": " + r.name
                 + " (" + std::to_string(r.obj_count) + " objects)";
        });
    return int(ExitCode::ok);
}
```

- [ ] **Step 4: Convert `do_object_list` and `do_config_list` the same way**

Define a `struct Row` per command (object: name, plate, instance count; config: key, value), build the vector, call `emit_list_response`.

- [ ] **Step 5: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS.

- [ ] **Step 6: Commit**

```powershell
git add src/cli/output.hpp src/cli/commands/plate.cpp src/cli/commands/object.cpp src/cli/commands/config.cpp tests/cli/unit/test_output.cpp
git commit -m "refactor(cli): emit_list_response unifies list-style commands (M2)"
```

---

## Task 11 — M11: Safer atomic rename in `save_project`

**Why:** `save_project` currently does `remove(target); rename(tmp, target)` which has a window on Windows where the destination does not exist. The passthrough path (`passthrough_missing_thumbnails`, lines 250-269 in current `io.cpp`) already uses the safer rename-to-.bak / rename-in / remove-.bak pattern. Adopt it for `save_project` too.

**Files:**
- Modify: `src/cli/io.cpp`
- Test: `tests/cli/roundtrip/test_save_atomic.cpp` (new)

- [ ] **Step 1: Write the failing test**

Create `tests/cli/roundtrip/test_save_atomic.cpp`:

```cpp
#include <catch2/catch.hpp>
#include "io.hpp"
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
using namespace orca_cli;

TEST_CASE("save_project leaves the destination existing throughout",
          "[orca-cli][cleanup][T11]") {
    auto tmp_dir = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tmp_dir);
    const auto dst = tmp_dir / "out.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, dst);

    auto state = load_project(dst.string());
    // The post-save file must exist (we cannot easily prove there's never
    // a delete window without instrumenting fs, but we can assert post-
    // condition + a missing .bak / .rewrite afterwards).
    REQUIRE_NOTHROW(save_project(state, dst.string()));
    REQUIRE(fs::exists(dst));
    REQUIRE_FALSE(fs::exists(dst.string() + ".tmp"));
    REQUIRE_FALSE(fs::exists(dst.string() + ".bak"));
    REQUIRE_FALSE(fs::exists(dst.string() + ".rewrite"));

    fs::remove_all(tmp_dir);
}
```

- [ ] **Step 2: Run the test (baseline; may pass since the unsafe window is small)**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli][cleanup][T11]" --order rand --warn NoAssertions
```

Expected: PASS (this pins the post-condition; the goal is to keep it passing after the refactor).

- [ ] **Step 3: Replace the rename block in `save_project`**

Find the existing `remove + rename` block (around `io.cpp:399-401`) and replace with the safer pattern (mirroring the passthrough path):

```cpp
boost::system::error_code ec;
const std::string bak = target_path + ".bak";
if (fs::exists(target_path)) {
    fs::rename(target_path, bak, ec);
    if (ec) throw std::runtime_error("save_project: rename existing -> .bak failed: " + ec.message());
}
fs::rename(tmp_path, target_path, ec);
if (ec) {
    boost::system::error_code ec2;
    if (fs::exists(bak)) fs::rename(bak, target_path, ec2); // best-effort restore
    throw std::runtime_error("save_project: rename .tmp -> target failed: " + ec.message());
}
if (fs::exists(bak)) fs::remove(bak, ec);
```

- [ ] **Step 4: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS.

- [ ] **Step 5: Commit**

```powershell
git add src/cli/io.cpp tests/cli/roundtrip/test_save_atomic.cpp
git commit -m "fix(cli): safer rename-to-.bak swap in save_project (M11)"
```

---

## Task 12 — M8: Split `unzip_to_memory` into name-only + targeted extract

**Why:** `unzip_to_memory` materializes every entry (including multi-MB `.model` mesh blobs) just so the invariant checks can read `.rels` + a list of PNG names. Halves peak memory and skips a wasteful full-archive decompress.

**Files:**
- Modify: `src/cli/invariants.hpp`
- Modify: `src/cli/invariants.cpp`
- Modify: `src/cli/io.cpp` (uses the new helpers in `run_all_invariants`)
- Test: `tests/cli/unit/test_invariants.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/cli/unit/test_invariants.cpp`:

```cpp
TEST_CASE("enumerate_zip_entry_names lists every non-directory entry",
          "[orca-cli][cleanup][T12]") {
    auto names = orca_cli::enumerate_zip_entry_names(ORCA_CLI_REF_3MF);
    REQUIRE(names.size() > 0);
    bool has_rels = false;
    for (const auto& n : names) if (n == "_rels/.rels") { has_rels = true; break; }
    REQUIRE(has_rels);
}

TEST_CASE("extract_entry_to_memory grabs just the rels bytes",
          "[orca-cli][cleanup][T12]") {
    auto bytes = orca_cli::extract_entry_to_memory(ORCA_CLI_REF_3MF, "_rels/.rels");
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() > 50); // sanity: a real .rels file
}
```

- [ ] **Step 2: Run test to verify it fails**

```powershell
& "build\tests\cli\RelWithDebInfo\cli_tests.exe" "[orca-cli][cleanup][T12]" --order rand --warn NoAssertions
```

Expected: compile error.

- [ ] **Step 3: Declare in `invariants.hpp`** (or a new `zip_io.hpp` if `invariants.hpp` should stay tight)

```cpp
// Cheap entry-name enumeration: opens the zip, walks the central directory,
// closes. Does NOT decompress entry bodies.
std::vector<std::string> enumerate_zip_entry_names(const std::string& zip_path);

// Extract a single entry by exact name. Returns nullopt if the entry is
// missing or the archive cannot be opened.
std::optional<std::vector<unsigned char>>
extract_entry_to_memory(const std::string& zip_path, const std::string& entry_name);
```

- [ ] **Step 4: Implement in `invariants.cpp`**

Use the same `open_zip_reader` / `mz_zip_reader_*` pattern that's already in this file. Implementation sketch:

```cpp
std::vector<std::string> enumerate_zip_entry_names(const std::string& zip_path) {
    std::vector<std::string> names;
    mz_zip_archive z{};
    if (!open_zip_reader(&z, zip_path)) return names;
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    names.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) continue;
        if (st.m_is_directory) continue;
        std::string name(st.m_filename);
        std::replace(name.begin(), name.end(), '\\', '/');
        names.push_back(std::move(name));
    }
    close_zip_reader(&z);
    return names;
}

std::optional<std::vector<unsigned char>>
extract_entry_to_memory(const std::string& zip_path, const std::string& entry_name) {
    mz_zip_archive z{};
    if (!open_zip_reader(&z, zip_path)) return std::nullopt;
    int idx = mz_zip_reader_locate_file(&z, entry_name.c_str(), nullptr, 0);
    if (idx < 0) { close_zip_reader(&z); return std::nullopt; }
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&z, mz_uint(idx), &st)) { close_zip_reader(&z); return std::nullopt; }
    std::vector<unsigned char> bytes(static_cast<size_t>(st.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(&z, mz_uint(idx), bytes.data(), bytes.size(), 0)) {
        close_zip_reader(&z); return std::nullopt;
    }
    close_zip_reader(&z);
    return bytes;
}
```

- [ ] **Step 5: Switch `run_all_invariants` callers**

In `verify_relationships`, replace the `unzip_to_memory` call with `extract_entry_to_memory(..., "_rels/.rels")` plus the existing regex scan. Replace the plate-thumbnail file-list check with `enumerate_zip_entry_names`. Stop calling `unzip_to_memory` for the cheap checks; keep it only where the full archive payload is actually required (likely nowhere, for these invariants).

- [ ] **Step 6: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS.

- [ ] **Step 7: Commit**

```powershell
git add src/cli/invariants.hpp src/cli/invariants.cpp tests/cli/unit/test_invariants.cpp
git commit -m "refactor(cli): name-only zip enumeration + targeted entry extract for invariants (M8)"
```

---

## Task 13 — M6: Split `passthrough_missing_thumbnails`

**Why:** 215-line function doing five things (orphan detection, source enumeration, target enumeration, in-memory copy, rewrite-swap). `mz_zip_reader_*` leaks across four code blocks — exactly what M8's helpers should encapsulate.

**Files:**
- Modify: `src/cli/io.cpp`

This task is large but mechanical. Take it in one commit; the boundaries are clear.

- [ ] **Step 1: Tests are existing** — every `plate add` / `plate remove` e2e test already exercises this code path. No new test needed; this task is graded by "still green."

- [ ] **Step 2: Extract `enumerate_target_entries`**

```cpp
struct TargetEntryInfo {
    std::unordered_set<std::string> kept_names;
    std::unordered_set<std::string> orphan_names;
};
TargetEntryInfo enumerate_target_entries(const std::string& target_zip_path, int plate_count);
```

Implementation: pull lines 91-116 of the current function (the target reader open + orphan check) into this helper. Use `mz_zip_reader_get_num_files` + `mz_zip_reader_file_stat`.

- [ ] **Step 3: Extract `plan_thumbnail_passthrough`**

```cpp
struct PassthroughPlan {
    std::vector<std::pair<mz_uint, std::string>> source_to_copy;   // src_index + name
    std::vector<std::string>                     to_synthesize;    // names
};
PassthroughPlan plan_thumbnail_passthrough(const ProjectState& s,
                                           const TargetEntryInfo& target,
                                           bool& src_opened_out,
                                           mz_zip_archive& src_reader_out);
```

Body: lines 117-163 of the current function (source enumeration + synthesize planning).

- [ ] **Step 4: Extract `rewrite_archive_with_blobs`**

```cpp
void rewrite_archive_with_blobs(
    const std::string&              target_zip_path,
    const std::string&              rewrite_path,
    const TargetEntryInfo&          target,
    const PassthroughPlan&          plan,
    mz_zip_archive&                 src_reader,
    bool                            src_opened,
    const std::vector<char>&        placeholder);
```

Body: lines 218-247 of the current function. Use `mz_zip_writer_add_from_zip_reader` for the source-copy entries (Task 14 also touches this — keep the integration to a single commit if convenient).

- [ ] **Step 5: Extract `atomic_swap_rewrite`**

```cpp
void atomic_swap_rewrite(const std::string& target_zip_path,
                         const std::string& rewrite_path);
```

Body: lines 250-269 (the rename-to-.bak / rename-in / remove-.bak pattern).

- [ ] **Step 6: Reduce `passthrough_missing_thumbnails` to a thin orchestrator**

```cpp
void passthrough_missing_thumbnails(const ProjectState& s,
                                    const std::string&  target_zip_path)
{
    using namespace boost::filesystem;
    auto target = enumerate_target_entries(target_zip_path, int(s.plates.size()));

    bool          src_opened = false;
    mz_zip_archive src_reader{};
    auto plan = plan_thumbnail_passthrough(s, target, src_opened, src_reader);

    if (plan.source_to_copy.empty() &&
        plan.to_synthesize.empty()  &&
        target.orphan_names.empty()) {
        if (src_opened) close_zip_reader(&src_reader);
        return;
    }

    std::vector<char> placeholder;
    if (!plan.to_synthesize.empty()) placeholder = make_placeholder_png_128_gray_C0();

    const std::string rewrite_path = target_zip_path + ".rewrite";
    rewrite_archive_with_blobs(target_zip_path, rewrite_path, target, plan,
                               src_reader, src_opened, placeholder);
    if (src_opened) close_zip_reader(&src_reader);
    atomic_swap_rewrite(target_zip_path, rewrite_path);
}
```

- [ ] **Step 7: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS — the behavior is identical, the call structure isn't.

- [ ] **Step 8: Commit**

```powershell
git add src/cli/io.cpp
git commit -m "refactor(cli): split passthrough_missing_thumbnails into focused helpers (M6)"
```

---

## Task 14 — H3: Skip recompression in passthrough

**Why:** `rewrite_archive_with_blobs` (post-Task 13) currently extracts every carried-forward entry to memory and re-adds with `MZ_DEFAULT_COMPRESSION`. For the ~99% of entries that are unchanged (every mesh blob, every config blob, every untouched PNG), use `mz_zip_writer_add_from_zip_reader` to copy raw deflated bytes — no decompress + recompress.

**Files:**
- Modify: `src/cli/io.cpp`

- [ ] **Step 1: Add a wall-time regression pin (optional)**

This is an optimization with no behavior change; existing tests prove correctness. If you want a perf pin:

```cpp
TEST_CASE("plate add completes under 5 seconds on reference 3mf",
          "[orca-cli][cleanup][T14][.perf]") {
    auto tmp = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(tmp);
    auto out = tmp / "x.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);

    auto t0 = std::chrono::steady_clock::now();
    auto s = load_project(out.string());
    add_plate(s, "perf_test_plate");
    save_project(s, out.string());
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - t0).count();
    INFO("plate add round trip: " << dt << "ms");
    REQUIRE(dt < 5000);

    fs::remove_all(tmp);
}
```

(The `[.perf]` tag means it's skipped by default; run with `cli_tests [.perf]`.)

- [ ] **Step 2: Switch the carried-forward write to `mz_zip_writer_add_from_zip_reader`**

In `rewrite_archive_with_blobs`, the loop that writes `tgt_blobs` (the in-memory extracted target entries) becomes a direct reader-to-writer copy. miniz expects the source reader and the writer to be both open at the same time, so keep the target reader open until the rewrite is done:

```cpp
mz_zip_archive tgt_reader{};
if (!open_zip_reader(&tgt_reader, target_zip_path)) return;
mz_zip_archive writer{};
if (!open_zip_writer(&writer, rewrite_path)) { close_zip_reader(&tgt_reader); return; }

const mz_uint tgt_count = mz_zip_reader_get_num_files(&tgt_reader);
for (mz_uint i = 0; i < tgt_count; ++i) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&tgt_reader, i, &st)) continue;
    if (st.m_is_directory) continue;
    std::string name(st.m_filename);
    std::replace(name.begin(), name.end(), '\\', '/');
    if (target.orphan_names.count(name)) continue; // drop orphan plate PNGs
    if (!mz_zip_writer_add_from_zip_reader(&writer, &tgt_reader, i)) {
        // Fallback to decompress + recompress on the off chance of failure.
        std::vector<unsigned char> bytes(static_cast<size_t>(st.m_uncomp_size));
        if (mz_zip_reader_extract_to_mem(&tgt_reader, i, bytes.data(), bytes.size(), 0)) {
            mz_zip_writer_add_mem(&writer, name.c_str(), bytes.data(), bytes.size(),
                                  MZ_DEFAULT_COMPRESSION);
        }
    }
}
// then: source-copy entries via mz_zip_writer_add_from_zip_reader against src_reader;
// then: synthesized placeholder PNG entries via mz_zip_writer_add_mem with MZ_NO_COMPRESSION
//       (PNG is already deflate-compressed; double-deflate just costs CPU).
mz_zip_writer_finalize_archive(&writer);
mz_zip_writer_end(&writer);
close_zip_reader(&tgt_reader);
```

Drop the `tgt_blobs` `std::vector<...>` entirely — no in-memory materialization is needed.

- [ ] **Step 3: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS — and meaningfully faster, especially for projects with large mesh blobs.

- [ ] **Step 4: Commit**

```powershell
git add src/cli/io.cpp
git commit -m "perf(cli): zero-recompress copy for carried-forward archive entries (H3)"
```

---

## Task 15 — H2: Avoid full re-parse in `verify_vector_config_roundtrip`

**Why:** `run_all_invariants` re-runs `load_bbs_3mf(..., LoadModel|LoadConfig)` on the just-saved archive — the most expensive operation in the pipeline — purely to diff vector-config keys. We already have the in-memory `ProjectState` and (post-Task 12) the ability to read just `Metadata/project_settings.config` without unzipping the full archive.

**Approach:** Replace `load_project` inside `verify_vector_config_roundtrip` with:
1. `extract_entry_to_memory(zip, "Metadata/project_settings.config")` → bytes.
2. Parse those bytes via `DynamicPrintConfig::load_from_*` (look at how libslic3r's bbs_3mf loader does it — `ConfigBase::load_string_xml` or `load_from_ini`, depending on format).
3. Compare vector keys against the in-memory `state.project_config`.

If parsing the standalone config from raw bytes is non-trivial in libslic3r's API, fall back to passing a `LoadConfig`-only flag to `load_bbs_3mf` (skip `LoadModel`, which is the heavy part).

**Files:**
- Modify: `src/cli/invariants.cpp`
- Modify: `src/cli/invariants.hpp` (signature may take the in-memory state)
- Modify: `src/cli/io.cpp` (caller passes the in-memory state into the invariant)
- Test: `tests/cli/unit/test_invariants.cpp` (vector-config roundtrip already exists; extend if needed)

- [ ] **Step 1: Decide between in-memory parse vs `LoadConfig`-only**

Read `src/libslic3r/Format/bbs_3mf.cpp` for the existing `load_bbs_3mf` signature and check whether it has a "config only, no model" flag. If yes, the cheapest fix is to switch the invariant's load to that flag. If not, use `extract_entry_to_memory` + `DynamicPrintConfig::load_from_ini_string` (or whatever the matching parser is).

```powershell
# Quick survey:
Select-String -Path src\libslic3r\Format\bbs_3mf.hpp -Pattern "LoadModel|LoadConfig|LoadStrategy"
```

- [ ] **Step 2: Implement the cheaper path**

Option A (preferred when supported) — `LoadConfig`-only:

```cpp
void verify_vector_config_roundtrip(const ProjectState& state, const std::string& zip_path) {
    using namespace Slic3r;
    DynamicPrintConfig roundtripped;
    LoadStrategy s = LoadStrategy::LoadConfig; // NOT LoadModel
    if (!load_bbs_3mf(zip_path.c_str(), &roundtripped, /*model*/ nullptr,
                      /*plates*/ nullptr, /*proj_presets*/ nullptr,
                      /*sub_ctx*/ nullptr, s)) {
        throw InvariantViolation("invariant: failed to re-read project config from " + zip_path);
    }
    // Existing diff logic against state.project_config goes here.
}
```

Option B — extract + parse:

```cpp
auto bytes = extract_entry_to_memory(zip_path, "Metadata/project_settings.config");
if (!bytes) throw InvariantViolation("invariant: project_settings.config missing in " + zip_path);
DynamicPrintConfig roundtripped;
std::string text(bytes->begin(), bytes->end());
ConfigSubstitutionContext ctx{ForwardCompatibilitySubstitutionRule::Disable};
roundtripped.load_from_ini_string(text, ctx);
// diff against state.project_config
```

- [ ] **Step 3: Build & run all CLI tests**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: all tests PASS — the vector-config roundtrip test from P1 still catches regressions, but the dominant cost on save is gone.

- [ ] **Step 4: Commit**

```powershell
git add src/cli/invariants.cpp src/cli/invariants.hpp src/cli/io.cpp tests/cli/unit/test_invariants.cpp
git commit -m "perf(cli): skip full LoadModel in verify_vector_config_roundtrip (H2)"
```

---

## Final sweep

- [ ] **Run the full CLI test suite once more from a clean build dir**

```powershell
cmake --build build --config RelWithDebInfo --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C RelWithDebInfo
```

Expected: 109+ tests PASS. Test count may increase slightly from the new unit tests added in this plan.

- [ ] **Manual GUI smoke** (per `docs/cli/manual-test.md`, cumulative P0..P7 recipe): run it end-to-end and open the resulting `.3mf` in OrcaSlicer to confirm no regressions.

- [ ] **Update `docs/cli/status.md`** with a "Cleanup pass" section summarizing the 15 commits (one bullet each).

- [ ] **Final commit**:

```powershell
git add docs/cli/status.md
git commit -m "docs(cli): record HIGH+MEDIUM cleanup pass in status (post-T15)"
```

---

## Self-Review Notes

- **Spec coverage:** the 15 tasks map 1:1 to the 15 HIGH/MEDIUM findings in the 2026-05-19 review (H1-H4, M1-M11). No finding is left unaddressed.
- **Ordering rationale:** helpers (T1-T5) → API shapes (T6-T7) → command-layer unification (T8) → JSON refactor (T9-T10) → I/O hot path (T11-T15). Each task only depends on tasks before it.
- **Test discipline:** every task either adds a new test or relies on existing tests that already exercise the path. The full suite is run after every task before commit.
- **Type consistency:** `MutationExceptionMap`, `ParsedVec3`, `AddObjectRawOpts`, `PlateThumbnailPaths`, `TargetEntryInfo`, `PassthroughPlan` are each defined in exactly one place and referenced consistently downstream.
- **Risk:** Task 9 (nlohmann::json) is the highest-risk task because it changes the exact JSON byte stream. Step 7 specifically calls out the need to convert string-comparison tests to parse-and-assert. Tasks 13-14 are large rewrites of one I/O function but their behavior is fully covered by existing P2 e2e tests.
