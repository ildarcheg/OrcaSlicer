# orca-cli `object merge-parts` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `orca-cli object merge-parts <file> --name X --parts X_1,X_3,... --into X_main [--filament N] [--output O]` — the natural inverse of `object split-to-parts`. Combines a named subset of ModelVolumes in one ModelObject into a single ModelVolume via triangle concatenation (with each source's `get_matrix()` baked in). The merged volume occupies the lowest-existing-index source's slot; other sources are erased.

**Architecture:** One new helper `merge_object_parts` in `src/cli/project_ops.{hpp,cpp}` plus one new exception type `DuplicateNameError` (case 8 ⇒ exit 5). All other refusals reuse `std::invalid_argument` (mapped to `invalid_state` ⇒ exit 7) or `std::out_of_range` (mapped to `unknown_reference` ⇒ exit 6). CLI dispatch in `src/cli/commands/object.cpp` adds the new verb through the existing `MutationExceptionMap` + `run_mutation` envelope. Validation runs in the fixed precedence order from spec Section 3.

**Spec:** `docs/superpowers/specs/2026-05-20-orca-cli-merge-parts-design.md` (approved at `b1d056571c`). The spec defines 7 brainstorm Q&As, 14-case exit-code matrix, 17 unit / 13 e2e / 1 roundtrip test plan, and Phase 9 doc deliverables.

**Tech Stack:** C++17, libslic3r, miniz (via libslic3r's `miniz_extension`), nlohmann::json, Catch2 v3 (`<catch2/catch_all.hpp>`), CMake/CTest, CLI11.

**Build & test commands** (Windows, PowerShell):

```powershell
cmake --build build --config Release --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C Release
```

Faster iteration on the new tag:

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge]" --order rand --warn NoAssertions
```

**Test conventions** (per `tests/CLAUDE.md` + the split-to-parts cleanup):
- Catch2 v3 (`<catch2/catch_all.hpp>`).
- No `&&` inside `REQUIRE` — split into separate `REQUIRE`s.
- `DYNAMIC_SECTION` in loops, not `SECTION`.
- All new tests tagged `[orca-cli][merge]`; e2e also `[e2e]`; roundtrip also `[roundtrip]`.

**Baseline before this plan:** main is at `b1d056571c` (3 design-spec commits + Phase 8 ship complete). The Phase 8 plan added ~24 new tests on top of the prior 124-baseline; current baseline is 148-ish (the implementer must capture the exact number in Task 0).

---

## File Structure Overview

| File | What it does | Action |
|------|--------------|--------|
| `src/cli/project_ops.hpp` | Declare `merge_object_parts` + `DuplicateNameError`. | **Modify** |
| `src/cli/project_ops.cpp` | Implement `merge_object_parts` with the 8-step validation precedence (Section 3) + bake-in + lowest-existing-index placement + source attribution. | **Modify** |
| `src/cli/commands/object.cpp` | Register `merge-parts` subcommand. `MutationExceptionMap` gets `.on<DuplicateNameError>(duplicate_name)` plus the same defaults split-to-parts uses. | **Modify** |
| `tests/cli/unit/test_project_ops.cpp` | +17 unit cases (Section 4 test list). | **Modify** |
| `tests/cli/e2e/test_merge.cpp` | New file: 13 e2e cases (happy path + integration chain + 11 anti-cases + `--output O`). | **Create** |
| `tests/cli/roundtrip/test_merge.cpp` | New file: 1 roundtrip case (volume count + merged name + extruder + per-vol config survive save/load). | **Create** |
| `docs/cli/manual-test.md` | Append Phase 9 section + cumulative recipe extension. | **Modify** |
| `docs/cli/status.md` | Add Phase 9 status block. | **Modify** |
| `~/.claude/projects/.../memory/project_orca_cli_v4.md` | New memory note after impl: ship status, test-count delta, GUI verification, NOT-pushed status. | **Create (memory)** |

No new test fixtures are needed. Existing Layer A `two_cubes.stl` (committed) drives the happy path; existing Layer B `box_with_text.stl` drives the realistic chain via split-to-parts → merge-parts; multi-source unit tests construct `ModelObject` instances directly in-test (the `[A,B,C,D]` shape from spec test #14, the distinct-attribution shape from spec test #13).

---

## Task 0: Pre-flight — capture baseline, verify multi-filament fixture

**Files:** none (read-only checks)

- [ ] **Step 1: Capture current test count and confirm green baseline**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" --order rand --warn NoAssertions 2>&1 | Select-String "test cases|assertions"
```

Expected: a single "All tests passed" line with `N test cases / M assertions`. Record N — this is the baseline count. Final-sweep target is `N + 17 + 13 + 1 = N + 31`.

- [ ] **Step 2: Verify the reference 3mf has `filament_slot_count >= 2`**

Run a one-off probe:

```powershell
& "build\tests\cli\Release\cli_tests.exe" "filament_slot_count >= 1 on reference" --order rand --warn NoAssertions
```

This existing test (in `test_project_ops.cpp`) asserts `>= 1`. Confirm it passes, then check the actual slot count by running:

```powershell
& "build\Release\orca-cli.exe" --json inspect "C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\temp_project_for_orca_slicer.3mf" | ConvertFrom-Json | Select-Object -ExpandProperty data
```

Look at the project's `filament_settings_id` length, or inspect the printer config in the reference 3mf. If it reports `>= 2`, proceed. If it reports `1`, **stop and notify the user** — the filament-conflict tests (Task 7 + e2e anti-case #11) need ≥2 slots to be meaningful, and the spec explicitly flagged this as a Task 0 verification. The remediation is either (a) the user authoring a 2-filament reference via the GUI, or (b) the tests constructing a multi-filament `ProjectState` in-memory by extending `filament_settings_id->values`.

- [ ] **Step 3: Confirm git state is clean**

```powershell
git status --short
```

Expected: empty output. If there are uncommitted changes, stash or commit them before starting the implementation.

(No commit for Task 0.)

---

## Task 1: API declaration + happy-path TDD

**Files:**
- Modify: `src/cli/project_ops.hpp`
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_project_ops.cpp`

- [ ] **Step 1: Write the failing happy-path test (unit test #1)**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("merge_object_parts on Layer A two-source merge produces 1 volume "
          "from 2",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_happy";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_happy"));
    auto* obj = find_object(s, "merge_happy");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_happy",
        {"merge_happy_1", "merge_happy_2"}, "merge_main", std::nullopt));

    auto* obj2 = find_object(s, "merge_happy");
    REQUIRE(obj2 != nullptr);
    REQUIRE(obj2->volumes.size() == 1);
    REQUIRE(obj2->volumes[0]->name == std::string("merge_main"));
}
```

- [ ] **Step 2: Verify the test fails to compile**

```powershell
cmake --build build --config Release --target cli_tests -- -m 2>&1 | Select-String "merge_object_parts" | Select-Object -First 3
```

Expected: compile error — `merge_object_parts` not declared.

- [ ] **Step 3: Declare `merge_object_parts` + `DuplicateNameError` in `src/cli/project_ops.hpp`**

Open `src/cli/project_ops.hpp` and append at the bottom of `namespace orca_cli` (after the `stamp_source_if_missing` declaration that ends at line 298):

```cpp
// DuplicateNameError: thrown by merge_object_parts when --into collides
// with an existing volume name on the object that is NOT one of the
// sources being consumed. Maps to ExitCode::duplicate_name (exit 5) via
// MutationExceptionMap::on<DuplicateNameError>. Mirrors PlacementFailure
// / BadConfigError as a typed std::runtime_error so the catch chain can
// distinguish "duplicate name" (exit 5) from "invalid state" (exit 7).
class DuplicateNameError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// merge_object_parts: combine the named subset of ModelVolumes within
// the named ModelObject into a single merged ModelVolume named
// `merged_part_name`. Mesh combination is triangle concatenation
// (TriangleMesh::merge) with each source's get_matrix() baked in before
// concat; the merged volume's matrix is set to identity. The merged
// volume is inserted at the position of the source with the LOWEST
// existing index in obj.volumes (stability under --parts reordering);
// the other sources are erased.
//
// Validation runs in the fixed precedence order from Section 3 of the
// spec. First failing check wins; subsequent checks are not evaluated.
//
//   throws std::out_of_range     if `object_name` is not on the project;
//                                if any name in `source_part_names` is
//                                not a volume on the object; or if
//                                filament_override is not in
//                                [1..filament_slot_count].
//   throws DuplicateNameError    if `merged_part_name` collides with an
//                                existing volume on the object that is
//                                NOT in `source_part_names`.
//   throws std::invalid_argument if any source volume is not MODEL_PART;
//                                if all source meshes are empty;
//                                if fewer than 2 non-empty sources
//                                remain after dropping empties;
//                                if non-empty sources have differing
//                                effective extruders AND
//                                filament_override is std::nullopt;
//                                or if non-empty sources have a
//                                per-volume non-extruder config key
//                                whose values diverge (strict rule).
void merge_object_parts(ProjectState& s,
                        const std::string&              object_name,
                        const std::vector<std::string>& source_part_names,
                        const std::string&              merged_part_name,
                        std::optional<int>              filament_override);
```

- [ ] **Step 4: Implement the minimal version in `src/cli/project_ops.cpp`**

This implementation handles ONLY the happy path. Subsequent tasks add each validation layer in spec precedence order. Append at the bottom of `namespace orca_cli` in `src/cli/project_ops.cpp` (after `split_object_to_parts` ends near line 666):

```cpp
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

    // Bake-in transform + concat. Lowest-existing-index = min element.
    const size_t anchor_idx =
        *std::min_element(source_indices.begin(), source_indices.end());

    TriangleMesh merged_mesh;
    for (size_t idx : source_indices) {
        ModelVolume* v = obj.volumes[idx];
        if (v->mesh().empty()) continue;
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
    ModelVolume* merged = obj.add_volume(merged_mesh);
    merged->name = merged_part_name;
    merged->set_transformation(Geometry::Transformation());
    merged->source = anchor_source;
    stamp_source_if_missing(*merged, anchor_source);

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
```

Note: `obj.add_volume` mutates `obj.volumes` (push_back). We rely on that to allocate `merged`, then we rebuild the vector. Subsequent tasks insert the validation layers ahead of the bake-in code.

- [ ] **Step 5: Add the required header includes**

At the top of `src/cli/project_ops.cpp`, ensure these are present (most should already be from prior tasks):

```cpp
#include <algorithm>  // std::min_element
```

(`<vector>`, `<optional>`, `<string>`, and the libslic3r headers are already in place from the existing implementation.)

- [ ] **Step 6: Run the happy-path test**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: 1 PASS (the test added in Step 1).

- [ ] **Step 7: Run the full split + merge suite for regression**

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split],[orca-cli][merge]" --order rand --warn NoAssertions
```

Expected: every split test still passes; new merge test passes.

- [ ] **Step 8: Commit**

```powershell
git add src/cli/project_ops.hpp src/cli/project_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "feat(cli): merge_object_parts API + happy path (T1 merge)"
```

---

## Task 2: Source lookup refusal — unknown object + unknown source

**Files:**
- Modify: `tests/cli/unit/test_project_ops.cpp`

Validation precedence step 2 (Section 3 cases 5, 6) is already enforced by the implementation in Task 1 (`find_object_or_throw` + the explicit "source part not found" throw). This task locks the behaviour in with unit tests.

- [ ] **Step 1: Write the failing tests (unit test #2 + companion for unknown object)**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("merge_object_parts on unknown object throws out_of_range "
          "(case 6)",
          "[orca-cli][merge][unit]") {
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    REQUIRE_THROWS_AS(
        merge_object_parts(s, "__missing__", {"a", "b"}, "m", std::nullopt),
        std::out_of_range);
}

TEST_CASE("merge_object_parts on unknown source name throws out_of_range "
          "(case 5)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_unknown_src";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_unknown_src"));

    REQUIRE_THROWS_AS(
        merge_object_parts(s, "merge_unknown_src",
            {"merge_unknown_src_1", "__nope__"}, "merged", std::nullopt),
        std::out_of_range);
}
```

- [ ] **Step 2: Run the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: both new tests PASS (the Task 1 implementation already throws the right exception types).

- [ ] **Step 3: Commit**

```powershell
git add tests/cli/unit/test_project_ops.cpp
git commit -m "test(cli): merge_object_parts source-lookup refusals (T2 merge)"
```

---

## Task 3: Name collision (cases 8, 9) — duplicate_name vs allow-when-source

**Files:**
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_project_ops.cpp`

Validation precedence step 3 (Section 3 cases 8, 9).

- [ ] **Step 1: Write the failing tests (unit tests #3, #4)**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("merge_object_parts refuses --into collision with non-source "
          "(case 8 -> DuplicateNameError)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_collide";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_collide"));

    // Add a third volume by hand-construction so we have a non-source
    // volume to collide with. Use an empty mesh to keep the test fast;
    // the collision check runs BEFORE empty-mesh validation in the
    // precedence chain (cases 12/13 come after case 8).
    ModelObject* obj = find_object(s, "merge_collide");
    REQUIRE(obj != nullptr);
    TriangleMesh dummy;
    ModelVolume* extra = obj->add_volume(dummy);
    extra->name = "merge_collide_extra";

    REQUIRE_THROWS_AS(
        merge_object_parts(s, "merge_collide",
            {"merge_collide_1", "merge_collide_2"},
            "merge_collide_extra",  // collides with non-source name
            std::nullopt),
        DuplicateNameError);
}

TEST_CASE("merge_object_parts allows --into matching a source name "
          "(case 9 -- source consumed, name reused)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_reuse";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_reuse"));

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_reuse",
        {"merge_reuse_1", "merge_reuse_2"},
        "merge_reuse_1",  // matches a source name -- allowed
        std::nullopt));

    auto* obj = find_object(s, "merge_reuse");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(obj->volumes[0]->name == std::string("merge_reuse_1"));
}
```

- [ ] **Step 2: Verify the collision test fails (no DuplicateNameError thrown yet)**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: "refuses --into collision" FAILS. "allows --into matching a source name" PASSES already (Task 1's logic naturally allows it — the source is erased, then the new volume gets the same name).

- [ ] **Step 3: Add the name-collision check to `merge_object_parts`**

In `src/cli/project_ops.cpp`, find the `merge_object_parts` function and insert after the source-indices loop (after the `source_indices.push_back(found)` block, before `// Bake-in transform + concat.`):

```cpp
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
```

Add `#include <unordered_set>` at the top of `src/cli/project_ops.cpp` if not already present.

- [ ] **Step 4: Run the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: both new tests PASS, plus the prior 3 unit tests.

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_ops.hpp src/cli/project_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "feat(cli): merge_object_parts name-collision check + DuplicateNameError (T3 merge)"
```

---

## Task 4: `--filament` range check (case 7)

**Files:**
- Modify: `src/cli/project_ops.cpp`

Validation precedence step 4 (Section 3 case 7). No dedicated unit test (the spec's unit-test list doesn't include this case; it's covered at the e2e layer in Task 13). This task adds the check in-place; future tasks reorder as needed.

- [ ] **Step 1: Add the `--filament` range check**

In `src/cli/project_ops.cpp`, find the `merge_object_parts` function and insert after the name-collision block (before `// Bake-in transform + concat.`):

```cpp
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
```

- [ ] **Step 2: Run the existing merge tests to confirm no regression**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: all 5 prior unit tests still PASS. (None pass `filament_override`, so the new check never fires for them.)

- [ ] **Step 3: Commit**

```powershell
git add src/cli/project_ops.cpp
git commit -m "feat(cli): merge_object_parts --filament range check (T4 merge, case 7)"
```

---

## Task 5: Source-volume-type check (case 11)

**Files:**
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_project_ops.cpp`

Validation precedence step 5 (Section 3 case 11). Unit test #5.

- [ ] **Step 1: Write the failing test (unit test #5)**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("merge_object_parts refuses non-MODEL_PART source "
          "(case 11 -> invalid_state)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_modifier";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_modifier"));

    // Convert the second volume into a modifier so it's no longer
    // MODEL_PART. The merge should refuse with invalid_argument
    // (maps to ExitCode::invalid_state at the CLI layer).
    ModelObject* obj = find_object(s, "merge_modifier");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    obj->volumes[1]->set_type(ModelVolumeType::PARAMETER_MODIFIER);

    REQUIRE_THROWS_AS(
        merge_object_parts(s, "merge_modifier",
            {"merge_modifier_1", "merge_modifier_2"},
            "merge_modifier_main", std::nullopt),
        std::invalid_argument);
}
```

- [ ] **Step 2: Verify the test fails (no MODEL_PART check yet)**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: the new test FAILS (the merge currently succeeds even with a modifier).

- [ ] **Step 3: Add the MODEL_PART check**

In `src/cli/project_ops.cpp`, find the `merge_object_parts` function and insert after the `--filament` range block (still before `// Bake-in transform + concat.`):

```cpp
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
```

- [ ] **Step 4: Run the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: 6 PASS.

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "feat(cli): merge_object_parts MODEL_PART check (T5 merge, case 11)"
```

---

## Task 6: Empty-mesh handling (cases 12, 13)

**Files:**
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_project_ops.cpp`

Validation precedence step 6 (Section 3 cases 12, 13). Unit test #6. Per spec Q7: silently skip empties, require ≥2 non-empty.

- [ ] **Step 1: Write the failing test (unit test #6)**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("merge_object_parts refuses when <2 non-empty sources remain "
          "(case 13 -> invalid_state)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_empty";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_empty"));

    // Replace one of the two volumes' meshes with an empty mesh,
    // simulating a stale / broken volume. After dropping empties only
    // 1 non-empty source remains -> case 13 refusal.
    ModelObject* obj = find_object(s, "merge_empty");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    obj->volumes[1]->reset_mesh();

    REQUIRE_THROWS_AS(
        merge_object_parts(s, "merge_empty",
            {"merge_empty_1", "merge_empty_2"},
            "merge_empty_main", std::nullopt),
        std::invalid_argument);
}
```

- [ ] **Step 2: Verify the test fails**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: the new test FAILS (the current implementation would silently emit a 1-source merge).

- [ ] **Step 3: Add the empty-mesh handling**

In `src/cli/project_ops.cpp`, find the `merge_object_parts` function and insert after the MODEL_PART block (still before `// Bake-in transform + concat.`):

```cpp
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
```

The bake-in loop a few lines below already has an `if (v->mesh().empty()) continue;` guard. Now change it to iterate `non_empty_indices` for clarity (the result is unchanged, but the read is clearer):

Replace:
```cpp
    TriangleMesh merged_mesh;
    for (size_t idx : source_indices) {
        ModelVolume* v = obj.volumes[idx];
        if (v->mesh().empty()) continue;
        TriangleMesh m(v->mesh());
        m.transform(v->get_matrix(), /*fix_left_handed=*/true);
        merged_mesh.merge(m);
    }
```

With:
```cpp
    TriangleMesh merged_mesh;
    for (size_t idx : non_empty_indices) {
        ModelVolume* v = obj.volumes[idx];
        TriangleMesh m(v->mesh());
        m.transform(v->get_matrix(), /*fix_left_handed=*/true);
        merged_mesh.merge(m);
    }
```

Anchor index now derives from `non_empty_indices` so an empty source can't be the anchor (its placement slot would be meaningless). Replace:
```cpp
    const size_t anchor_idx =
        *std::min_element(source_indices.begin(), source_indices.end());
```

With:
```cpp
    // Anchor = lowest existing index among NON-EMPTY sources. An empty
    // source has no geometry to anchor and would produce a confusing
    // inspect-output if it stole the slot.
    const size_t anchor_idx =
        *std::min_element(non_empty_indices.begin(), non_empty_indices.end());
```

- [ ] **Step 4: Run the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: 7 PASS (the prior 6 + the new empty-mesh refusal).

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "feat(cli): merge_object_parts empty-mesh handling (T6 merge, cases 12+13)"
```

---

## Task 7: Filament agreement, smart default, override (case 10 + tests #7, #8, #9, #15, #16)

**Files:**
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_project_ops.cpp`

Validation precedence step 7 (Section 3 case 10). Spec Q4: smart default + `--filament` override; empty sources excluded from agreement.

- [ ] **Step 1: Write the 5 failing tests (unit tests #7, #8, #9, #15, #16)**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
namespace {
// Helper for the filament-agreement tests. Returns the effective
// extruder of a volume: per-volume override if set, else object-level
// override if set, else 1. Mirrors object_volume_info's logic.
int effective_extruder(const Slic3r::ModelObject& obj,
                       const Slic3r::ModelVolume& v) {
    using namespace Slic3r;
    if (auto* ve = v.config.opt<ConfigOptionInt>("extruder"))
        return ve->value;
    if (auto* oe = obj.config.opt<ConfigOptionInt>("extruder"))
        return oe->value;
    return 1;
}
} // namespace

TEST_CASE("merge_object_parts inherits filament when sources agree "
          "(test #9, no --filament needed)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_fil_agree";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_fil_agree"));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_agree", 2,
        std::optional<std::string>("merge_fil_agree_1")));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_agree", 2,
        std::optional<std::string>("merge_fil_agree_2")));

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_fil_agree",
        {"merge_fil_agree_1", "merge_fil_agree_2"},
        "merge_fil_agree_main", std::nullopt));

    auto* obj = find_object(s, "merge_fil_agree");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(effective_extruder(*obj, *obj->volumes[0]) == 2);
}

TEST_CASE("merge_object_parts refuses filament conflict without override "
          "(case 10 -> invalid_state, test #7)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_fil_conflict";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_fil_conflict"));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_conflict", 1,
        std::optional<std::string>("merge_fil_conflict_1")));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_conflict", 2,
        std::optional<std::string>("merge_fil_conflict_2")));

    REQUIRE_THROWS_AS(
        merge_object_parts(s, "merge_fil_conflict",
            {"merge_fil_conflict_1", "merge_fil_conflict_2"},
            "merge_fil_conflict_main", std::nullopt),
        std::invalid_argument);
}

TEST_CASE("merge_object_parts applies filament override on conflict "
          "(test #8)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_fil_over";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_fil_over"));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_over", 1,
        std::optional<std::string>("merge_fil_over_1")));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_over", 2,
        std::optional<std::string>("merge_fil_over_2")));

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_fil_over",
        {"merge_fil_over_1", "merge_fil_over_2"},
        "merge_fil_over_main", std::optional<int>(2)));

    auto* obj = find_object(s, "merge_fil_over");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(effective_extruder(*obj, *obj->volumes[0]) == 2);
}

TEST_CASE("merge_object_parts honours explicit --filament override on "
          "agreeing sources (test #15)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_fil_explicit";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_fil_explicit"));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_explicit", 1,
        std::optional<std::string>("merge_fil_explicit_1")));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_explicit", 1,
        std::optional<std::string>("merge_fil_explicit_2")));

    // Sources agree on extruder=1; override to 2 must win.
    REQUIRE_NOTHROW(merge_object_parts(s, "merge_fil_explicit",
        {"merge_fil_explicit_1", "merge_fil_explicit_2"},
        "merge_fil_explicit_main", std::optional<int>(2)));

    auto* obj = find_object(s, "merge_fil_explicit");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(effective_extruder(*obj, *obj->volumes[0]) == 2);
}

TEST_CASE("merge_object_parts excludes empty sources from filament "
          "agreement check (test #16)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_fil_skipempty";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_fil_skipempty"));

    // Add a third volume (initially with non-empty mesh from add_volume
    // -- the only way to construct a ModelVolume), then reset its mesh
    // and set its extruder to a deliberately-conflicting value. The
    // empty source must NOT contribute to the agreement check.
    ModelObject* obj = find_object(s, "merge_fil_skipempty");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    TriangleMesh dummy(obj->volumes[0]->mesh()); // copy a non-empty mesh
    ModelVolume* extra = obj->add_volume(dummy);
    extra->name = "merge_fil_skipempty_empty";
    extra->config.set("extruder", 2);
    extra->reset_mesh();   // now empty

    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_skipempty", 1,
        std::optional<std::string>("merge_fil_skipempty_1")));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_skipempty", 1,
        std::optional<std::string>("merge_fil_skipempty_2")));

    // Three "sources" — the empty one has ext=2 but should be excluded.
    // The two non-empty sources agree on ext=1; no --filament needed.
    REQUIRE_NOTHROW(merge_object_parts(s, "merge_fil_skipempty",
        {"merge_fil_skipempty_1", "merge_fil_skipempty_2",
         "merge_fil_skipempty_empty"},
        "merge_fil_skipempty_main", std::nullopt));

    auto* obj2 = find_object(s, "merge_fil_skipempty");
    REQUIRE(obj2 != nullptr);
    REQUIRE(obj2->volumes.size() == 1);
    REQUIRE(effective_extruder(*obj2, *obj2->volumes[0]) == 1);
}
```

- [ ] **Step 2: Verify the tests fail**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: at least 3 of the 5 new tests FAIL. (The "no override on conflict" test may pass for the wrong reason — the current implementation silently emits the merged volume with the anchor's filament. The override tests fail because the merged volume doesn't yet carry an extruder. The "inherit on agreement" test may pass coincidentally if the anchor happens to inherit correctly.)

- [ ] **Step 3: Add the filament-agreement block**

In `src/cli/project_ops.cpp`, find the `merge_object_parts` function and insert after the empty-mesh block, BEFORE the bake-in loop:

```cpp
    // Section 3 precedence step 7: filament agreement (case 10).
    // Empty sources (already dropped above) are excluded from the
    // agreement check per Q4 + Q7 of the spec. Compute the unique set
    // of effective extruders across non-empty sources.
    int object_extruder = 1;
    if (auto* oe = obj.config.opt<Slic3r::ConfigOptionInt>("extruder")) {
        object_extruder = oe->value;
    }
    std::set<int> non_empty_extruders;
    for (size_t idx : non_empty_indices) {
        int eff = object_extruder;
        if (auto* ve = obj.volumes[idx]->config.opt<Slic3r::ConfigOptionInt>("extruder")) {
            eff = ve->value;
        }
        non_empty_extruders.insert(eff);
    }

    int merged_extruder = 0;
    if (filament_override.has_value()) {
        // Explicit override wins regardless of agreement.
        merged_extruder = *filament_override;
    } else if (non_empty_extruders.size() == 1) {
        // All non-empty sources agree -- inherit.
        merged_extruder = *non_empty_extruders.begin();
    } else {
        throw std::invalid_argument(
            "cannot merge: source parts have different filament "
            "assignments and no --filament override was supplied. "
            "Either unify filaments first via "
            "'orca-cli object set-filament --part Y --filament N', "
            "or pass '--filament N' to merge-parts.");
    }
```

Add `#include <set>` at the top of `src/cli/project_ops.cpp` if not already present.

Then in the existing merged-volume construction block (after `merged->source = anchor_source;`), add the extruder write:

Replace:
```cpp
    merged->name = merged_part_name;
    merged->set_transformation(Geometry::Transformation());
    merged->source = anchor_source;
    stamp_source_if_missing(*merged, anchor_source);
```

With:
```cpp
    merged->name = merged_part_name;
    merged->set_transformation(Geometry::Transformation());
    merged->source = anchor_source;
    stamp_source_if_missing(*merged, anchor_source);
    merged->config.set("extruder", merged_extruder);
```

And delete the now-stale tail comment + `(void)filament_override;` line.

- [ ] **Step 4: Run the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: 12 PASS (the 7 prior + 5 new filament tests).

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "feat(cli): merge_object_parts filament agreement + override (T7 merge, case 10)"
```

---

## Task 8: Per-volume non-extruder config strict rule (case 14 + tests #10, #11, #17)

**Files:**
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_project_ops.cpp`

Validation precedence step 8 (Section 3 case 14). Spec Q6 strict rule: all-carry-same OR none-carry; otherwise refuse.

- [ ] **Step 1: Write the 3 failing tests (unit tests #10, #11, #17)**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("merge_object_parts inherits per-vol config when sources agree "
          "(test #10)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_cfg_agree";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_cfg_agree"));

    // Both sources carry wall_loops=4.
    ModelObject* obj = find_object(s, "merge_cfg_agree");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    obj->volumes[0]->config.set("wall_loops", 4);
    obj->volumes[1]->config.set("wall_loops", 4);

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_cfg_agree",
        {"merge_cfg_agree_1", "merge_cfg_agree_2"},
        "merge_cfg_agree_main", std::nullopt));

    auto* obj2 = find_object(s, "merge_cfg_agree");
    REQUIRE(obj2 != nullptr);
    REQUIRE(obj2->volumes.size() == 1);
    auto* wl = obj2->volumes[0]->config.opt<ConfigOptionInt>("wall_loops");
    REQUIRE(wl != nullptr);
    REQUIRE(wl->value == 4);
}

TEST_CASE("merge_object_parts refuses per-vol config conflict between "
          "carriers (case 14 -> invalid_state, test #11)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_cfg_conflict";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_cfg_conflict"));

    ModelObject* obj = find_object(s, "merge_cfg_conflict");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    obj->volumes[0]->config.set("wall_loops", 4);
    obj->volumes[1]->config.set("wall_loops", 7);

    try {
        merge_object_parts(s, "merge_cfg_conflict",
            {"merge_cfg_conflict_1", "merge_cfg_conflict_2"},
            "merge_cfg_conflict_main", std::nullopt);
        FAIL("expected std::invalid_argument");
    } catch (const std::invalid_argument& e) {
        std::string msg(e.what());
        INFO("error message: " << msg);
        REQUIRE(msg.find("wall_loops") != std::string::npos);
    }
}

TEST_CASE("merge_object_parts strict rule rejects mixed carry/no-carry "
          "(test #17)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_cfg_mixed";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_cfg_mixed"));

    // Source 1 carries wall_loops=5; source 2 does NOT carry the key.
    // Object-level wall_loops=3. Strict rule rejects mixed carry.
    ModelObject* obj = find_object(s, "merge_cfg_mixed");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    obj->config.set("wall_loops", 3);
    obj->volumes[0]->config.set("wall_loops", 5);
    // obj->volumes[1] deliberately left without the key.

    try {
        merge_object_parts(s, "merge_cfg_mixed",
            {"merge_cfg_mixed_1", "merge_cfg_mixed_2"},
            "merge_cfg_mixed_main", std::nullopt);
        FAIL("expected std::invalid_argument");
    } catch (const std::invalid_argument& e) {
        std::string msg(e.what());
        INFO("error message: " << msg);
        REQUIRE(msg.find("wall_loops") != std::string::npos);
    }
}
```

- [ ] **Step 2: Verify the tests fail**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: the conflict / mixed tests FAIL (current implementation does not check per-vol config). The agree test may pass or fail depending on whether the anchor's config is being copied to the merged volume.

- [ ] **Step 3: Add the per-vol config check + stash for merged-volume application**

Add the includes at the top of `src/cli/project_ops.cpp` (if not already present from Task 7):

```cpp
#include <map>
```

In `src/cli/project_ops.cpp`, find the `merge_object_parts` function. Insert two pieces:

**Piece A** — directly after the empty-mesh block (after the `non_empty_indices` filter + size check), declare the stash and run the agreement check. This sits BEFORE the filament-agreement block from Task 7:

```cpp
    // Section 3 precedence step 8: per-volume non-extruder config
    // agreement (case 14, strict rule per spec Q6).
    //
    // For every per-volume key that appears on any non-empty source:
    //   * all non-empty sources carry the key with the same value -> inherit
    //   * none of the non-empty sources carry the key            -> drop
    //   * any other mix (some carry, some don't, or values differ) -> refuse
    //
    // Empty sources are already excluded (non_empty_indices). Stash
    // the agreed serializations into a function-scope map so the
    // merged-volume construction below can apply them after the bake-in.
    std::map<std::string, std::string> agreed_per_vol_config;
    {
        std::set<std::string> all_keys;
        for (size_t idx : non_empty_indices) {
            for (const auto& kv : obj.volumes[idx]->config.get()) {
                all_keys.insert(kv.first);
            }
        }
        all_keys.erase("extruder"); // handled separately below

        std::vector<std::string> conflicts;
        for (const auto& key : all_keys) {
            std::vector<std::string> carrier_values;
            int                      carrier_count = 0;
            for (size_t idx : non_empty_indices) {
                const auto& cfg = obj.volumes[idx]->config;
                if (cfg.has(key)) {
                    ++carrier_count;
                    carrier_values.push_back(cfg.opt_serialize(key));
                }
            }
            const int n = int(non_empty_indices.size());
            if (carrier_count == 0) {
                // Nobody carries -- harmless; the key showed up only as
                // a default-traversal artifact (shouldn't happen since
                // all_keys was built from carriers). Skip.
                continue;
            } else if (carrier_count == n) {
                // All carry -- they must agree.
                const auto& first = carrier_values.front();
                const bool same = std::all_of(carrier_values.begin(),
                                              carrier_values.end(),
                    [&](const std::string& s) { return s == first; });
                if (same) {
                    agreed_per_vol_config[key] = first;
                } else {
                    conflicts.push_back(key);
                }
            } else {
                // Mixed carry / no-carry -- strict rule rejects.
                conflicts.push_back(key);
            }
        }
        if (!conflicts.empty()) {
            std::string msg = "cannot merge: per-volume config keys "
                              "diverge across sources: ";
            for (size_t i = 0; i < conflicts.size(); ++i) {
                if (i > 0) msg += ", ";
                msg += conflicts[i];
            }
            msg += ". To merge, set the per-volume value on all source "
                   "parts first via 'config set --object X --part Y "
                   "--key K --value V', then retry.";
            throw std::invalid_argument(msg);
        }
    }
```

**Piece B** — in the merged-volume construction block (after the existing `merged->config.set("extruder", merged_extruder);` line from Task 7), apply the agreed values:

```cpp
    // Apply agreed per-volume config keys (strict rule, Section 2 Q6).
    // ConfigSubstitutionContext is the same type set_project_config /
    // set_object_config use; deserialize matches what the GUI does on
    // 3mf reopen.
    for (const auto& kv : agreed_per_vol_config) {
        Slic3r::ConfigSubstitutionContext subs(
            Slic3r::ForwardCompatibilitySubstitutionRule::Disable);
        merged->config.set_deserialize(kv.first, kv.second, subs);
    }
```

If `merged->config.set_deserialize(key, value, subs)` doesn't compile directly (the `ModelConfigObject` API may differ from `DynamicPrintConfig::set_deserialize`), use the existing `set_object_config` body in `src/cli/project_ops.cpp` as the reference — it already deserializes a string value into a per-object `ModelConfigObject` via the same context type.

- [ ] **Step 4: Run the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: 15 PASS (12 prior + 3 new).

- [ ] **Step 5: Commit**

```powershell
git add src/cli/project_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "feat(cli): merge_object_parts strict per-vol config rule (T8 merge, case 14)"
```

---

## Task 9: Bake-in transform + lowest-existing-index placement (tests #12, #14)

**Files:**
- Modify: `tests/cli/unit/test_project_ops.cpp`

The bake-in code is already in place from Task 1; the lowest-existing-index anchor was added in Task 6 (the `non_empty_indices` `min_element`). This task locks both behaviours in with focused unit tests.

- [ ] **Step 1: Write the failing tests (unit tests #12, #14)**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("merge_object_parts bakes in source get_matrix() before concat "
          "(test #12 -- AABB sanity)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_bake";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_bake"));

    // Apply a translation matrix to source 2 only. Bake-in must produce
    // a merged AABB that reflects the offset.
    ModelObject* obj = find_object(s, "merge_bake");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    const BoundingBoxf3 b1_pre = obj->volumes[0]->mesh().bounding_box();
    Transform3d offset = Transform3d::Identity();
    offset.translate(Vec3d(100.0, 0.0, 0.0));
    obj->volumes[1]->set_transformation(Geometry::Transformation(offset));

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_bake",
        {"merge_bake_1", "merge_bake_2"},
        "merge_bake_main", std::nullopt));

    auto* obj2 = find_object(s, "merge_bake");
    REQUIRE(obj2 != nullptr);
    REQUIRE(obj2->volumes.size() == 1);
    const BoundingBoxf3 merged_bb = obj2->volumes[0]->mesh().bounding_box();
    // Pre-merge, source 1 lived around [0..10] in x and source 2 around
    // [30..40]. After applying a +100 translation matrix to source 2,
    // source 2's vertices in the baked mesh are at [130..140]. The
    // merged AABB max.x must reach at least 130.
    INFO("merged bbox: min=" << merged_bb.min.x() << "," << merged_bb.min.y() << "," << merged_bb.min.z()
         << "  max=" << merged_bb.max.x() << "," << merged_bb.max.y() << "," << merged_bb.max.z()
         << "  pre-merge source1 bbox: min=" << b1_pre.min.x() << ",max=" << b1_pre.max.x());
    REQUIRE(merged_bb.max.x() >= 130.0 - 1e-3);
}

TEST_CASE("merge_object_parts places merged volume at LOWEST-EXISTING-INDEX "
          "source slot regardless of --parts order (test #14)",
          "[orca-cli][merge][unit]") {
    using namespace orca_cli;
    using namespace Slic3r;

    // Construct an in-test object with 4 named volumes A,B,C,D using the
    // same TriangleMesh shape for each (non-empty, MODEL_PART). No need
    // to go through the full ProjectState pipeline; we just need a
    // ProjectState whose model has an object whose volumes vector we can
    // inspect. Build minimal scaffolding.
    ProjectState s;
    s.project_config = std::make_unique<DynamicPrintConfig>();
    // filament_settings_id needs >= 1 entry so filament_slot_count >= 1.
    auto* fsid = new ConfigOptionStrings();
    fsid->values = {"Generic PLA"};
    s.project_config->set_key_value("filament_settings_id", fsid);
    s.model          = std::make_unique<Model>();
    s.plates.push_back(std::make_unique<PlateData>());

    ModelObject* obj = s.model->add_object();
    obj->name = "ABCD";
    // Add 4 volumes with distinct non-empty meshes. Use 1mm cubes at
    // four different positions so bake-in doesn't collapse them.
    auto make_cube = [](Vec3d origin) {
        // 1mm AABB cube via indexed_triangle_set. Reuse two_cubes.stl
        // would also work but bypassing STL load keeps this test fast.
        TriangleMesh m;
        // Construct a unit cube by 12 facets. Use libslic3r's helper if
        // available, otherwise inline.
        Pointf3s pts;
        pts.push_back(origin);
        pts.push_back(origin + Vec3d(1,0,0));
        pts.push_back(origin + Vec3d(1,1,0));
        pts.push_back(origin + Vec3d(0,1,0));
        pts.push_back(origin + Vec3d(0,0,1));
        pts.push_back(origin + Vec3d(1,0,1));
        pts.push_back(origin + Vec3d(1,1,1));
        pts.push_back(origin + Vec3d(0,1,1));
        std::vector<Vec3i32> tris {
            {0,2,1}, {0,3,2},  // bottom
            {4,5,6}, {4,6,7},  // top
            {0,1,5}, {0,5,4},  // front
            {1,2,6}, {1,6,5},  // right
            {2,3,7}, {2,7,6},  // back
            {3,0,4}, {3,4,7},  // left
        };
        indexed_triangle_set its;
        its.vertices.reserve(pts.size());
        for (const auto& p : pts) its.vertices.emplace_back(float(p.x()), float(p.y()), float(p.z()));
        its.indices = std::move(tris);
        m = TriangleMesh(its);
        return m;
    };
    ModelVolume* va = obj->add_volume(make_cube(Vec3d(0,  0, 0))); va->name = "A";
    ModelVolume* vb = obj->add_volume(make_cube(Vec3d(2,  0, 0))); vb->name = "B";
    ModelVolume* vc = obj->add_volume(make_cube(Vec3d(4,  0, 0))); vc->name = "C";
    ModelVolume* vd = obj->add_volume(make_cube(Vec3d(6,  0, 0))); vd->name = "D";

    // Merge {C, A, B} (non-monotonic in --parts order). Anchor should
    // be A (lowest index = 0). Resulting volumes: [merged, D].
    REQUIRE_NOTHROW(merge_object_parts(s, "ABCD",
        {"C", "A", "B"}, "M", std::nullopt));

    REQUIRE(obj->volumes.size() == 2);
    REQUIRE(obj->volumes[0]->name == std::string("M"));
    REQUIRE(obj->volumes[1]->name == std::string("D"));
}
```

If `indexed_triangle_set` / `Vec3i32` / the `Pointf3s` helpers aren't already pulled in by the test file's includes, add:

```cpp
#include <admesh/stl.h>  // indexed_triangle_set
```

(libslic3r's `TriangleMesh.hpp` typically re-exports these; check what the existing `test_project_ops.cpp` and the project's `TriangleMesh` ctors require.)

- [ ] **Step 2: Verify the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit]" --order rand --warn NoAssertions
```

Expected: 17 PASS (15 prior + 2 new). If the bake-in test fails on the AABB assertion, the `mesh().bounding_box()` call may return a cached pre-transform box — `ModelVolume::mesh()` returns the *intrinsic* mesh (untransformed). The merged volume's bake-in stores the transformed vertices directly into the merged TriangleMesh, so `merged->mesh().bounding_box()` reflects the post-bake-in geometry. Confirm by debugging the AABB if it fails.

- [ ] **Step 3: Commit**

```powershell
git add tests/cli/unit/test_project_ops.cpp
git commit -m "test(cli): merge_object_parts bake-in + lowest-existing-index placement lock-in (T9 merge)"
```

---

## Task 10: Source attribution lowest-existing-index — Bug C lock-in (test #13)

**Files:**
- Modify: `tests/cli/unit/test_project_ops.cpp`

The source attribution code is already in place from Task 1 (`anchor_source = obj.volumes[anchor_idx]->source` + `merged->source = anchor_source`) and the anchor was tightened in Task 6 to lowest-non-empty-index. This task locks in the contract per spec test #13: merged volume's `source.input_file` matches the lowest-existing-index source's `source.input_file`, exercised with **distinct** per-source attributions.

- [ ] **Step 1: Write the failing test (unit test #13)**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("merge_object_parts: merged source.input_file matches "
          "LOWEST-EXISTING-INDEX source's attribution (Bug C lock-in, "
          "test #13)",
          "[orca-cli][merge][unit][source-attribution]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_src";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_src"));

    // Add a third volume by hand-construction so we have 3 sources
    // with DISTINCT attributions (simulating a hand-authored multi-
    // volume object, the only case where attribution actually
    // diverges across sources).
    ModelObject* obj = find_object(s, "merge_src");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    // Set source.input_file for each split-derived volume to a distinct
    // simulated path so the "all sources from one split share input_file"
    // dominant-case shortcut doesn't apply.
    obj->volumes[0]->source.input_file = "/sim/a.stl";
    obj->volumes[0]->name = "A";
    obj->volumes[1]->source.input_file = "/sim/b.stl";
    obj->volumes[1]->name = "B";
    // Add a third volume by cloning volume 0's mesh.
    TriangleMesh m(obj->volumes[0]->mesh());
    ModelVolume* vc = obj->add_volume(m);
    vc->name = "C";
    vc->source.input_file = "/sim/c.stl";

    // Merge {C, A, B} (non-monotonic). Anchor is A (lowest index = 0).
    REQUIRE_NOTHROW(merge_object_parts(s, "merge_src",
        {"C", "A", "B"}, "merged", std::nullopt));

    auto* obj2 = find_object(s, "merge_src");
    REQUIRE(obj2 != nullptr);
    REQUIRE(obj2->volumes.size() == 1);
    REQUIRE(obj2->volumes[0]->name == std::string("merged"));
    INFO("merged source.input_file=" << obj2->volumes[0]->source.input_file);
    REQUIRE(obj2->volumes[0]->source.input_file == std::string("/sim/a.stl"));
}

TEST_CASE("merge_object_parts: dominant post-split case keeps shared "
          "input_file on merged volume (sub-assertion of test #13)",
          "[orca-cli][merge][unit][source-attribution]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_src_shared";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_src_shared"));

    auto* obj = find_object(s, "merge_src_shared");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    const std::string before = obj->volumes[0]->source.input_file;
    REQUIRE_FALSE(before.empty());
    REQUIRE(obj->volumes[1]->source.input_file == before);

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_src_shared",
        {"merge_src_shared_1", "merge_src_shared_2"},
        "merge_src_shared_main", std::nullopt));

    auto* obj2 = find_object(s, "merge_src_shared");
    REQUIRE(obj2 != nullptr);
    REQUIRE(obj2->volumes.size() == 1);
    REQUIRE(obj2->volumes[0]->source.input_file == before);
}
```

- [ ] **Step 2: Run the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][unit][source-attribution]" --order rand --warn NoAssertions
```

Expected: 2 PASS. If the distinct-attribution test fails, the anchor selection isn't picking the lowest-existing-index source — re-check Task 6's `min_element(non_empty_indices)` change.

- [ ] **Step 3: Commit**

```powershell
git add tests/cli/unit/test_project_ops.cpp
git commit -m "test(cli): merge_object_parts source-attribution lock-in (T10 merge, Bug C)"
```

---

## Task 11: CLI `object merge-parts` subcommand wiring + happy e2e smoke

**Files:**
- Modify: `src/cli/commands/object.cpp`
- Create: `tests/cli/e2e/test_merge.cpp`

- [ ] **Step 1: Write the failing e2e smoke test**

Create `tests/cli/e2e/test_merge.cpp`:

```cpp
// orca-cli object merge-parts end-to-end tests.
#include <catch2/catch_all.hpp>
#include "test_common.hpp"
#include "archive_invariants.hpp"
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

namespace fs = boost::filesystem;

TEST_CASE("object merge-parts happy path: 2-source merge produces 1 volume "
          "with inspect --json showing merged name + filament",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "merge_happy.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";

    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name","mp"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","mp"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","mp","--parts","mp_1,mp_2","--into","mp_main"}).exit_code == 0);

    auto rc = orca_cli_test::run_cli({"--json","inspect",out.string()});
    REQUIRE(rc.exit_code == 0);
    auto j = nlohmann::json::parse(rc.stdout_);
    REQUIRE(j["status"] == "ok");
    bool found = false;
    for (const auto& o : j["data"]["objects"]) {
        if (o["name"] == "mp") {
            found = true;
            REQUIRE(o.contains("volumes"));
            REQUIRE(o["volumes"].size() == 1);
            REQUIRE(o["volumes"][0]["name"] == "mp_main");
        }
    }
    REQUIRE(found);

    fs::remove_all(tmp);
}
```

- [ ] **Step 2: Verify the test fails (merge-parts not registered)**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][e2e]" --order rand --warn NoAssertions
```

Expected: FAIL on the `merge-parts` CLI invocation — CLI11 returns non-zero exit code because the subcommand isn't registered.

- [ ] **Step 3: Register the subcommand in `src/cli/commands/object.cpp`**

In `src/cli/commands/object.cpp`, find the `register_object_subcmd` function. After the existing `static std::string split_file, split_name;` line near line 226, add:

```cpp
    // Phase 9: state for `object merge-parts`.
    static std::string merge_file, merge_name, merge_parts, merge_into;
    static int         merge_filament = 0;  // 0 means "unset"
```

After the existing `split` subcommand block (ends around line 322), add:

```cpp
    // -- object merge-parts (Phase 9) -------------------------------------
    // Combine a subset of an object's ModelVolumes into a single merged
    // ModelVolume. Natural inverse of split-to-parts.
    //
    // Exception mapping:
    //   DuplicateNameError    -> ExitCode::duplicate_name  (exit 5, case 8)
    //   std::out_of_range     -> ExitCode::unknown_reference (exit 6,
    //                                cases 5, 6, 7)
    //   std::invalid_argument -> ExitCode::invalid_state   (exit 7,
    //                                cases 10, 11, 12, 13, 14)
    auto* merge = obj->add_subcommand("merge-parts",
        "combine a subset of an object's parts into a single merged part");
    merge->add_option("file", merge_file, "input .3mf path")->required();
    merge->add_option("--name", merge_name,
                      "name of the parent object")->required();
    merge->add_option("--parts", merge_parts,
                      "comma-separated list of source volume names (>=2)")
        ->required();
    merge->add_option("--into", merge_into,
                      "name for the resulting merged volume")->required();
    merge->add_option("--filament", merge_filament,
                      "explicit filament slot to assign to the merged volume "
                      "(1-based); required when sources have differing extruders");
    merge->add_option("--output", g.output,
                      "write result to this path instead of overwriting input");
    merge->callback([&g]() {
        // Section 3 precedence step 1: parse-level validation (usage_error).
        if (merge_into.empty()) {
            print_err(g, ExitCode::usage_error,
                "merge-parts --into must be non-empty");
            std::exit(int(ExitCode::usage_error));
        }
        // Split the comma list. Empty -> case 1; size==1 -> case 2;
        // duplicates -> case 3. All map to usage_error.
        std::vector<std::string> parts;
        {
            std::string cur;
            for (char c : merge_parts) {
                if (c == ',') {
                    if (!cur.empty()) parts.push_back(cur);
                    cur.clear();
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) parts.push_back(cur);
        }
        if (parts.empty()) {
            print_err(g, ExitCode::usage_error,
                "merge-parts --parts must be non-empty (>=2 source names)");
            std::exit(int(ExitCode::usage_error));
        }
        if (parts.size() < 2) {
            print_err(g, ExitCode::usage_error,
                "merge-parts requires >=2 source parts");
            std::exit(int(ExitCode::usage_error));
        }
        {
            std::set<std::string> seen;
            for (const auto& n : parts) {
                if (!seen.insert(n).second) {
                    print_err(g, ExitCode::usage_error,
                        "merge-parts --parts contains duplicate name '" +
                        n + "'");
                    std::exit(int(ExitCode::usage_error));
                }
            }
        }

        std::optional<int> filament_override;
        if (merge_filament != 0) filament_override = merge_filament;

        MutationExceptionMap em;
        em.on<DuplicateNameError>(ExitCode::duplicate_name)
          .set_default_invalid_argument(ExitCode::invalid_state)
          .set_default_out_of_range(ExitCode::unknown_reference);
        std::exit(run_mutation(g, merge_file,
            "merge parts of object '" + merge_name + "' into '" +
                merge_into + "'", em,
            [parts, filament_override](ProjectState& s) {
                merge_object_parts(s, merge_name, parts,
                                   merge_into, filament_override);
            }));
    });
```

Add `#include <set>` at the top of `src/cli/commands/object.cpp` if not already present.

- [ ] **Step 4: Run the e2e smoke test**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][e2e]" --order rand --warn NoAssertions
```

Expected: PASS.

- [ ] **Step 5: Run the entire merge unit suite to confirm no regression**

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge]" --order rand --warn NoAssertions
```

Expected: 17 unit + 1 e2e = 18 PASS.

- [ ] **Step 6: Commit**

```powershell
git add src/cli/commands/object.cpp tests/cli/e2e/test_merge.cpp
git commit -m "feat(cli): orca-cli object merge-parts subcommand + happy e2e (T11 merge)"
```

---

## Task 12: E2E anti-cases — all 11 remaining exit-code paths + `--output O`

**Files:**
- Modify: `tests/cli/e2e/test_merge.cpp`

Covers e2e tests #3-#13 from the Section 4 list.

- [ ] **Step 1: Append all 11 anti-case tests + the `--output O` test**

Append to `tests/cli/e2e/test_merge.cpp`:

```cpp
// Helper: set up a 2-volume object via split-to-parts. Returns the
// project path so the merge subcommand has something to merge.
static fs::path make_split_project(const fs::path& tmp,
                                   const std::string& obj_name) {
    const auto out = tmp / (obj_name + ".3mf");
    fs::copy_file(ORCA_CLI_REF_3MF, out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name",obj_name}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name",obj_name}).exit_code == 0);
    return out;
}

TEST_CASE("merge-parts exits 1 when --parts has 1 entry (case 2)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "single");
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","single","--parts","single_1","--into","main"});
    REQUIRE(rc.exit_code == 1);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 1 when --parts has duplicate names (case 3)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "dup");
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","dup","--parts","dup_1,dup_1","--into","main"});
    REQUIRE(rc.exit_code == 1);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 1 when --into is empty (case 4)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "noemptyinto");
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","noemptyinto","--parts","noemptyinto_1,noemptyinto_2",
        "--into",""});
    REQUIRE(rc.exit_code == 1);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 5 when --into collides with non-source (case 8)",
          "[orca-cli][merge][e2e]") {
    // Need a third (non-source) volume on the object to collide with.
    // Simplest path: split to N parts, then merge only a subset, then
    // attempt a second merge whose --into name = one of the survivors.
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "collide.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);
    const auto stl = fs::path(ORCA_CLI_STL_DIR) / "box_with_text.stl";
    if (!fs::exists(stl)) SKIP("box_with_text.stl not present at " << stl.string());
    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name","col"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","col"}).exit_code == 0);
    // box_with_text.stl produces N>=3 parts. First merge col_1+col_2 into
    // col_merged, leaving col_3, col_4, ... untouched.
    REQUIRE(orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","col","--parts","col_1,col_2","--into","col_merged"}).exit_code == 0);
    // Now try to merge col_3 + col_4 INTO col_merged. col_merged exists
    // and is NOT in --parts; expect exit 5 (duplicate_name).
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","col","--parts","col_3,col_4","--into","col_merged"});
    REQUIRE(rc.exit_code == 5);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 6 when source name not on object (case 5)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "unknownsrc");
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","unknownsrc","--parts","unknownsrc_1,__nope__","--into","main"});
    REQUIRE(rc.exit_code == 6);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 6 when --name object not found (case 6)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "unknownobj.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","__nope__","--parts","a,b","--into","m"});
    REQUIRE(rc.exit_code == 6);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 6 when --filament out of range (case 7)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "filoor");
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","filoor","--parts","filoor_1,filoor_2","--into","m",
        "--filament","9999"});
    REQUIRE(rc.exit_code == 6);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 7 on filament conflict without --filament "
          "(case 10)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "filconf");
    // Assign distinct filaments to the two parts so the merge sees
    // disagreement. Requires filament_slot_count >= 2 (verified at T0).
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","filconf","--part","filconf_1","--filament","1"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","filconf","--part","filconf_2","--filament","2"}).exit_code == 0);
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","filconf","--parts","filconf_1,filconf_2","--into","m"});
    REQUIRE(rc.exit_code == 7);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 7 on per-vol config conflict (case 14, "
          "message lists keys)",
          "[orca-cli][merge][e2e]") {
    // Construct the conflict via config set --object X --part Y once
    // such a verb exists; right now the CLI surface does NOT include
    // a per-part config writer (spec marks it out of scope). For this
    // e2e, we simulate the conflict by setting wall_loops at the object
    // level on one project, then editing per-volume config in-place via
    // a follow-up command... actually the CLI doesn't expose this path
    // either. SKIP this e2e until a per-part config CLI verb lands;
    // unit tests #11 and #17 cover the rule at the library layer.
    SKIP("per-vol config conflict cannot be authored via the CLI today; "
         "covered by unit tests #11 and #17 at the project_ops layer");
}

TEST_CASE("merge-parts --output O writes only the side-car file",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto in_ = make_split_project(tmp, "sidecar");
    const auto out = tmp / "merged.3mf";
    const auto in_size_before = fs::file_size(in_);

    REQUIRE(orca_cli_test::run_cli({"object","merge-parts",in_.string(),
        "--name","sidecar","--parts","sidecar_1,sidecar_2","--into","m",
        "--output",out.string()}).exit_code == 0);
    REQUIRE(fs::exists(out));
    REQUIRE(fs::file_size(in_) == in_size_before);
    fs::remove_all(tmp);
}

TEST_CASE("end-to-end Layer B realistic mesh: split-to-parts then "
          "merge-parts on a subset (integration test #2)",
          "[orca-cli][merge][e2e]") {
    // Note: This test is intentionally an integration test that chains
    // Phase 8 split-to-parts with Phase 9 merge-parts; a Phase 8 regression
    // will surface here as a non-merge-parts failure. The focused
    // merge-parts e2e tests (#1, #3-#13) use Layer A directly without
    // going through split-to-parts.
    using namespace orca_cli_test::archive;
    const auto stl = fs::path(ORCA_CLI_STL_DIR) / "box_with_text.stl";
    if (!fs::exists(stl)) SKIP("box_with_text.stl not present at " << stl.string());

    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "merge_layerb.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);

    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name","realB"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","realB"}).exit_code == 0);
    // Set differential filament on two parts so we know the merge needs
    // an --filament override.
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","realB","--part","realB_1","--filament","1"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","realB","--part","realB_2","--filament","2"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","realB","--parts","realB_1,realB_2",
        "--into","realB_main","--filament","1"}).exit_code == 0);

    // Archive invariants still hold after the merge.
    assert_parts_have_source_file(out);

    // Inspect should show one fewer volume on realB.
    auto rc = orca_cli_test::run_cli({"--json","inspect",out.string()});
    REQUIRE(rc.exit_code == 0);
    auto j = nlohmann::json::parse(rc.stdout_);
    bool found = false;
    for (const auto& o : j["data"]["objects"]) {
        if (o["name"] == "realB") {
            found = true;
            REQUIRE(o.contains("volumes"));
            // box_with_text.stl produces N>=3 parts. We merged 2 -> 1,
            // so volume count is N-1. Just check the merged name is
            // present.
            bool has_main = false;
            for (const auto& v : o["volumes"]) {
                if (v["name"] == "realB_main") has_main = true;
            }
            REQUIRE(has_main);
        }
    }
    REQUIRE(found);

    fs::remove_all(tmp);
}
```

- [ ] **Step 2: Run all e2e tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][e2e]" --order rand --warn NoAssertions
```

Expected: ~13 e2e tests run (the per-vol-conflict one SKIPs because the CLI can't author that conflict; the case-8 collision and Layer B integration SKIP when `box_with_text.stl` is absent). All non-SKIP cases PASS.

- [ ] **Step 3: Commit**

```powershell
git add tests/cli/e2e/test_merge.cpp
git commit -m "test(cli): merge-parts e2e anti-cases + Layer B integration (T12 merge)"
```

---

## Task 13: Roundtrip — volume count + merged name + extruder + per-vol config

**Files:**
- Create: `tests/cli/roundtrip/test_merge.cpp`

Section 4 roundtrip test #1.

- [ ] **Step 1: Write the failing test**

Create `tests/cli/roundtrip/test_merge.cpp`:

```cpp
// Roundtrip test for orca-cli merge-parts. Verifies that the merged
// volume's name, extruder, and per-volume config survive a full
// save -> reopen cycle via libslic3r's bbs_3mf reader.
#include <catch2/catch_all.hpp>
#include "test_common.hpp"
#include "io.hpp"
#include "project_ops.hpp"
#include <boost/filesystem.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

namespace fs = boost::filesystem;

TEST_CASE("merge-parts: merged volume name + extruder + per-vol config "
          "survive save/load roundtrip",
          "[orca-cli][merge][roundtrip]") {
    using namespace orca_cli;
    using namespace Slic3r;
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "merge_rt.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);

    {
        ProjectState s = load_project(out.string());
        AddObjectParams p;
        p.plate_name  = s.plates.front()->plate_name;
        p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
        p.object_name = "rt_merge";
        p.count       = 1;
        REQUIRE_NOTHROW(add_object(s, p));
        REQUIRE_NOTHROW(split_object_to_parts(s, "rt_merge"));
        // Stamp identical filament on both so merge inherits without --filament.
        REQUIRE_NOTHROW(set_object_filament(s, "rt_merge", 2,
            std::optional<std::string>("rt_merge_1")));
        REQUIRE_NOTHROW(set_object_filament(s, "rt_merge", 2,
            std::optional<std::string>("rt_merge_2")));
        // Stamp identical wall_loops on both so merge inherits.
        auto* obj_pre = find_object(s, "rt_merge");
        REQUIRE(obj_pre != nullptr);
        obj_pre->volumes[0]->config.set("wall_loops", 4);
        obj_pre->volumes[1]->config.set("wall_loops", 4);

        REQUIRE_NOTHROW(merge_object_parts(s, "rt_merge",
            {"rt_merge_1", "rt_merge_2"},
            "rt_merge_main", std::nullopt));
        REQUIRE_NOTHROW(save_project(s, out.string()));
    }

    ProjectState s2 = load_project(out.string());
    auto* obj = find_object(s2, "rt_merge");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(obj->volumes[0]->name == std::string("rt_merge_main"));
    auto* ext = obj->volumes[0]->config.opt<ConfigOptionInt>("extruder");
    REQUIRE(ext != nullptr);
    REQUIRE(ext->value == 2);
    auto* wl = obj->volumes[0]->config.opt<ConfigOptionInt>("wall_loops");
    REQUIRE(wl != nullptr);
    REQUIRE(wl->value == 4);

    fs::remove_all(tmp);
}
```

- [ ] **Step 2: No CMake edit needed**

`tests/cli/CMakeLists.txt` already uses `file(GLOB CONFIGURE_DEPENDS ... roundtrip/*.cpp ...)` (lines 18-24), so the new `roundtrip/test_merge.cpp` is picked up automatically on next CMake reconfigure. Re-run the build to make sure the new file is included:

```powershell
cmake --build build --config Release --target cli_tests -- -m
```

If the build doesn't pick up the new file, force a reconfigure:

```powershell
cmake -B build
cmake --build build --config Release --target cli_tests -- -m
```

- [ ] **Step 3: Run the test**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][merge][roundtrip]" --order rand --warn NoAssertions
```

Expected: PASS. If the `wall_loops` assertion fails after reopen, the serializer is not writing per-volume non-extruder config keys to `Metadata/model_settings.config`. Check `bbs_3mf.cpp` `store_bbs_3mf` for `<part>` element serialization of `ModelConfigObject` keys other than `extruder` and `name`.

- [ ] **Step 4: Commit**

```powershell
git add tests/cli/roundtrip/test_merge.cpp
git commit -m "test(cli): roundtrip for merged volume name + extruder + per-vol config (T13 merge)"
```

---

## Task 14: Docs + memory

**Files:**
- Modify: `docs/cli/manual-test.md`
- Modify: `docs/cli/status.md`
- Create: `~/.claude/projects/.../memory/project_orca_cli_v4.md`

- [ ] **Step 1: Append Phase 9 section to `docs/cli/manual-test.md`**

Open `docs/cli/manual-test.md` and append after the existing Phase 8 section:

```markdown
## Phase 9 - `object merge-parts`

Inverse of Phase 8's split-to-parts. Requires `box_with_text.stl` in
`$STLS` (copy from `C:\Users\ildarcheg\Documents\GitHub\` once per
session) for the realistic recipe.

```powershell
$OUT = "$env:TEMP\orca-cli-p9.3mf"
Copy-Item $REF $OUT -Force
& $CLI object add $OUT --plate "Plate 1" --stl "$STLS\box_with_text.stl" --name multipart
& $CLI object split-to-parts $OUT --name multipart                # N parts
& $CLI object set-filament $OUT --name multipart --part multipart_1 --filament 1
& $CLI object set-filament $OUT --name multipart --part multipart_2 --filament 2
# Merge two parts back together with an explicit filament:
& $CLI object merge-parts $OUT --name multipart `
    --parts multipart_1,multipart_2 --into multipart_combo --filament 1
& $CLI --json inspect $OUT | ConvertFrom-Json | ConvertTo-Json -Depth 5
```

Expected: `multipart` has N-1 volumes. The merged `multipart_combo`
volume occupies the slot where `multipart_1` used to be (lowest existing
index of the two sources). The remaining non-merged parts retain their
filament assignments.

Anti-cases (each should exit non-zero):
```powershell
& $CLI object merge-parts $OUT --name multipart `
    --parts multipart_combo --into x
# expected: exit 1 (usage_error -- requires >=2 source parts)

& $CLI object merge-parts $OUT --name multipart `
    --parts multipart_3,nope --into x
# expected: exit 6 (unknown_reference -- 'nope' not on object)

& $CLI object merge-parts $OUT --name multipart `
    --parts multipart_3,multipart_4 --into multipart_combo
# expected: exit 5 (duplicate_name -- collides with non-source)

& $CLI object merge-parts $OUT --name multipart `
    --parts multipart_3,multipart_4 --into x --filament 9999
# expected: exit 6 (unknown_reference -- slot out of range)
```

Manual GUI smoke: open `$OUT` in OrcaSlicer; the `multipart` object
should appear with N-1 parts in the object panel, with the merged
`multipart_combo` carrying the explicit filament.
```

- [ ] **Step 2: Update the cumulative recipe at the end of Phase 8**

Find the cumulative recipe in `docs/cli/manual-test.md`. After the existing Phase 8 lines (`object add multi`, `split-to-parts multi`, `set-filament --part multi_1 --filament 1`), append:

```powershell
& $CLI object merge-parts $OUT --name multi --parts multi_1,multi_2 --into multi_combo --filament 1
```

- [ ] **Step 3: Append Phase 9 to `docs/cli/status.md`**

Open `docs/cli/status.md` and append after the existing Phase 8 section:

```markdown
## Phase 9 - object merge-parts

- [x] `merge_object_parts` in `project_ops.cpp` implements Section 3's
  8-step validation precedence (parse / lookup / collision / filament
  range / MODEL_PART / empty-mesh / filament agreement / per-vol config
  strict rule), bakes each source's `get_matrix()` into a single
  `TriangleMesh::merge`, places the result at the LOWEST-EXISTING-INDEX
  source's slot, and stamps source attribution from the same anchor.
  Does NOT call libslic3r's `ModelObject::merge_volumes` (three confirmed
  bugs documented in spec Section 2).
- [x] New typed exception `DuplicateNameError` (case 8 -> exit 5,
  duplicate_name). All other refusals reuse `std::invalid_argument`
  (default -> invalid_state, exit 7) or `std::out_of_range` (default ->
  unknown_reference, exit 6).
- [x] `orca-cli object merge-parts <file> --name X --parts ... --into Y
  [--filament N] [--output O]` end-to-end via the standard load-mutate-
  save flow + `MutationExceptionMap`.
- [x] `inspect --json`: pre-existing `volumes` array from Phase 8 reports
  the merged volume; volume count drops by N-1 (for an N-source merge).
- [x] Bug C lock-in: merged volume's `source.input_file` matches the
  lowest-existing-index source's attribution, exercised with both the
  dominant post-split shared-input case and the distinct-attribution
  hand-authored case (unit test #13).
- [x] All P0-P8 tests still pass (regression).
- [ ] Manual GUI smoke: open the P9 manual-test output in OrcaSlicer and
  verify the merged object renders as one ModelVolume per part vector
  entry, with the merged part carrying the assigned filament. (Pending
  separate manual verification.)
```

- [ ] **Step 4: Create the memory note**

Create `~/.claude/projects/C--Users-ildarcheg-Documents-GitHub-OrcaSlicer/memory/project_orca_cli_v4.md` (path resolved via the harness's per-project memory dir; do NOT commit). Content:

```markdown
---
name: project-orca-cli-v4
description: "orca-cli v4 (merge-parts) shipped 2026-05-DD; +N tests; NOT pushed; GUI verified."
metadata:
  node_type: memory
  type: project
---

orca-cli v4 ships the `object merge-parts` verb, the inverse of Phase 8's
`split-to-parts`. Section 2 of the spec
(`docs/superpowers/specs/2026-05-20-orca-cli-merge-parts-design.md`)
locks the implementation locus in `src/cli/project_ops.cpp` (not the
buggy `ModelObject::merge_volumes` libslic3r call). Validation runs in
the Section 3 deterministic precedence order.

## Status

- **Branch:** `main`, local only. NOT pushed per [[feedback-autonomous-execution]].
- **Test count:** N cases / M assertions (delta +31: 17 unit / 13 e2e / 1
  roundtrip), all green under
  `cli_tests.exe --order rand --warn NoAssertions`.
- **GUI smoke:** verified in OrcaSlicer on YYYY-MM-DD.

## Key implementation choices (echoes of the spec, locked into code)

- Lowest-existing-index source wins both placement AND attribution.
  Argument order of `--parts` never affects output.
- Bake-in: each source's `get_matrix()` is applied to a copy of its
  mesh before concat; the merged volume's matrix is identity.
- Empty sources are silently dropped from concat AND from filament /
  per-vol-config agreement checks. <2 non-empty sources -> refuse.
- Strict per-vol non-extruder config rule: all-carry-same OR none-carry,
  otherwise refuse with a hint message.
- Bug C: source.input_file stamped from the anchor source, exercised
  with both shared-attribution (post-split) and distinct-attribution
  (hand-authored) fixtures.

## Deferred / out-of-scope (future work)

- Per-volume config CLI verb (would unblock e2e for case 14, currently SKIP).
- CSG union path (future `object union-parts`).
- Cross-object merge.
- Pattern / regex `--match` selection.

## How to apply

- Run `docs/cli/manual-test.md` Phase 9 recipe + cumulative recipe.
- For implementation reference: spec at
  `docs/superpowers/specs/2026-05-20-orca-cli-merge-parts-design.md`,
  plan at `docs/superpowers/plans/2026-05-20-orca-cli-merge-parts.md`.
```

Then update `MEMORY.md` to add a one-line entry for the new memory note (under or near the existing v2 / v2_cleanup entries).

- [ ] **Step 5: Commit the docs (memory files are NOT committed)**

```powershell
git add docs/cli/manual-test.md docs/cli/status.md
git commit -m "docs(cli): Phase 9 merge-parts + cumulative recipe update (T14 merge)"
```

---

## Final sweep

- [ ] **Run the full cli_tests suite once more**

```powershell
cmake --build build --config Release --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C Release
```

Expected: baseline (N from Task 0) + 17 unit + 13 e2e + 1 roundtrip = **N + 31** tests PASS. Two e2e tests (case 8 collision, Layer B integration) SKIP when `box_with_text.stl` is absent. One e2e (case 14 conflict) SKIPs unconditionally — covered by unit tests #11 and #17.

- [ ] **Manual GUI smoke** per `docs/cli/manual-test.md` Phase 9 + the appended line in the cumulative recipe. After verification, edit the Phase 9 status block in `docs/cli/status.md` to mark the GUI-smoke checkbox as `[x]` and commit:

```powershell
git add docs/cli/status.md
git commit -m "docs(cli): mark Phase 9 manual GUI smoke verified (YYYY-MM-DD)"
```

- [ ] **Do NOT push** unless the user explicitly asks. Per [[feedback-autonomous-execution]], all orca-cli work to date is local-only on `main`.

---

## Self-Review Notes

**1. Spec coverage** — each spec section mapped to one or more tasks:

- **Spec §1 CLI surface** — T11 (subcommand registration + flags + happy e2e).
- **Spec §2 Behaviour semantics:**
  - Implementation locus → T1 (direct in `project_ops.cpp`, exception types declared).
  - Bake-in transformations → T1 (loop) + T9 (lock-in test).
  - Source volume placement (lowest-existing-index) → T1 (anchor calc) + T6 (anchor restricted to non-empty) + T9 (lock-in test).
  - Per-volume non-extruder config strict rule → T8.
  - Source attribution → T1 (anchor stamp) + T10 (lock-in test, distinct attributions).
  - Save-pipeline invariants → no new code; existing guards apply.
  - `invalidate_bounding_box` → T1 (call after swap).
- **Spec §3 Edge cases / exit codes:**
  - Cases 1, 2, 3, 4 (usage_error) → T11 (CLI parser block).
  - Cases 5, 6 (unknown_reference) → T1 (`find_object_or_throw` + source-lookup) + T2 (lock-in tests).
  - Case 7 (filament range) → T4 + T12 e2e.
  - Cases 8, 9 (duplicate_name vs allow) → T3 + T12 e2e.
  - Case 10 (filament conflict) → T7 + T12 e2e.
  - Case 11 (MODEL_PART) → T5 + T12 e2e implied through invalid_state mapping (no dedicated CLI test — covered at unit level).
  - Cases 12, 13 (empty mesh) → T6.
  - Case 14 (per-vol config) → T8 (unit tests #11, #17; e2e SKIPs because no per-part config CLI verb).
  - **Validation order subsection** → enforced by the order in which tasks insert checks into `merge_object_parts`; each task inserts its check at the correct precedence slot.
  - **Idempotency** subsection → no code change required (falls naturally out of "second invocation can't find consumed sources -> exit 6"); covered implicitly by the unit + e2e suite.
- **Spec §4 Tests** — every numbered test is mapped:
  - Unit #1 → T1, #2 → T2, #3-#4 → T3, #5 → T5, #6 → T6, #7-#9, #15-#16 → T7, #10-#11, #17 → T8, #12, #14 → T9, #13 → T10.
  - E2E #1 → T11, #2 (Layer B) → T12, #3-#13 → T12.
  - Roundtrip #1 → T13.
- **Spec §5 Documentation** — T14 covers all four deliverables (manual-test Phase 9, status Phase 9, cumulative recipe extension, memory note).

**2. Placeholder scan** — zero TBDs / TODOs / "implement later". Every step contains the actual code or command needed. The two intentional SKIP cases (case 8 e2e when `box_with_text.stl` is absent; case 14 e2e because no per-part config CLI verb exists) are documented with explicit `SKIP()` calls and rationales, not placeholders.

**3. Type / signature consistency:**

- `merge_object_parts(ProjectState&, const std::string&, const std::vector<std::string>&, const std::string&, std::optional<int>)` — declared T1, used in every subsequent test + the CLI callback.
- `DuplicateNameError : std::runtime_error` — declared T1, thrown in T3, caught via `.on<DuplicateNameError>(ExitCode::duplicate_name)` in T11.
- `effective_extruder(const ModelObject&, const ModelVolume&)` — helper in `tests/cli/unit/test_project_ops.cpp` from T7, used by T7's filament tests.
- `make_split_project(const fs::path&, const std::string&)` — helper in `tests/cli/e2e/test_merge.cpp` from T12, used by every T12 anti-case.
- All `set_object_filament(s, name, slot, optional<part_name>)` calls match the existing Phase 8 signature.
- All `ModelConfigObject` access (`v->config.set("extruder", N)`, `v->config.opt<ConfigOptionInt>("extruder")`, `v->config.set_deserialize(key, value, subs)`) matches the existing project_ops + commands pattern.

**Risks / open items for the implementer:**

- The `s_agreed_for_merge` stash in T8 is a function-local; the variable name is hinting at "save until needed". Rename to `agreed_per_vol_config` for clarity at implementation time if desired — the test code doesn't depend on the name.
- T9's in-test `make_cube` lambda uses `indexed_triangle_set` + `Vec3i32`. If the libslic3r headers in `test_project_ops.cpp` don't already expose these, the implementer must add the missing include (`<admesh/stl.h>` typically, or `<libslic3r/TriangleMesh.hpp>` re-exports). The split tests construct `TriangleMesh` from STL loads only, so this is a new pattern in this test file.
- T8's per-vol config strict-rule implementation uses `ModelConfigObject::get()` to enumerate keys; if the real type uses a different accessor (e.g. `keys()` or iteration via `cbegin`/`cend`), adapt accordingly. The existing `object_config_keys` helper in `project_ops.cpp` already enumerates per-object config keys — read that for the right idiom before writing T8 step 3.
- The exact filament_slot_count of the reference 3mf is verified at T0 step 2. If it turns out to be `1`, T7's filament-conflict unit tests and T12's case 10 e2e need a multi-filament fixture instead. The implementer must surface that to the user before proceeding past Task 0.
