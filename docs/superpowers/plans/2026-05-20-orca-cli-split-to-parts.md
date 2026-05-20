# orca-cli `object split-to-parts` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `orca-cli object split-to-parts <file> --name X` that decomposes a single-volume ModelObject into multiple ModelVolumes (one per connected mesh component), plus extend `orca-cli object set-filament` with a `--part Y` flag for per-volume filament assignment. Extend `inspect` to show per-volume info.

**Architecture:** One new helper (`split_object_to_parts`) in `src/cli/project_ops.{hpp,cpp}` delegating to libslic3r's `ModelVolume::split` (`src/libslic3r/Model.cpp:2742`). Existing `set_object_filament` gets an optional `part_name` parameter. CLI dispatch in `src/cli/commands/object.cpp` adds the new verb and the `--part` flag, both routed through the existing `MutationExceptionMap` + `run_mutation` envelope. New testable helper `stamp_source_if_missing` enforces Bug C source-attribution defense with its own dedicated TDD cycle.

**Tech Stack:** C++17, libslic3r, miniz (via libslic3r's `miniz_extension`), nlohmann::json, Catch2 v3 (`<catch2/catch_all.hpp>`), CMake/CTest, CLI11.

**Build & test commands** (Windows, PowerShell):

```powershell
$env:Path = "C:\Program Files\CMake\bin;" + $env:Path  # one-time per shell if cmake isn't on PATH
cmake --build build --config Release --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C Release
```

For a faster iteration on the new tag:

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split]" --order rand --warn NoAssertions
```

**Test conventions** (per `tests/CLAUDE.md` and the cleanup pass):
- Catch2 v3 (`<catch2/catch_all.hpp>`).
- No `&&` inside `REQUIRE`.
- `DYNAMIC_SECTION` in loops, not `SECTION`.
- All new tests tagged `[orca-cli][split]`; sanity tests also `[fixture]`; e2e also `[e2e]`; roundtrip also `[roundtrip]`.

**Baseline before this plan:** 124 tests / 66046+ assertions green on `main` at HEAD `3b49685f9d` (post-cleanup, post-push). Branch is on the user's fork at `origin/main`.

---

## File Structure Overview

| File | What it does | Action |
|------|--------------|--------|
| `tests/cli/fixtures/two_cubes.stl` | Committed binary STL: two 10mm cubes at `(0,0,0)` and `(30,0,0)`. Layer A fixture. ~700 bytes. | **Create** (committed) |
| `tests/cli/fixtures/gen_minimal_stls.cpp` | One-off generator that writes `two_cubes.stl`. Built as a CMake executable; running it is idempotent. | **Create** |
| `tests/cli/CMakeLists.txt` | Add `gen_minimal_stls` target + `ORCA_CLI_FIXTURES_DIR` compile-time define on `cli_tests`. | **Modify** |
| `src/cli/project_ops.hpp` | Declare `split_object_to_parts`, change `set_object_filament` signature to take `optional<part_name>`, declare `object_volume_info` + `VolumeInfo` struct, declare `stamp_source_if_missing`. | **Modify** |
| `src/cli/project_ops.cpp` | Implement the four. `split_object_to_parts` validates preconditions, captures parent source, calls `ModelVolume::split`, then calls `stamp_source_if_missing` on every resulting volume. | **Modify** |
| `src/cli/commands/object.cpp` | Add `split-to-parts` subcommand; extend `set-filament` with `--part` option; route both through `run_mutation` with default `MutationExceptionMap` (with `invalid_argument` -> `invalid_state` for split). | **Modify** |
| `src/cli/commands/inspect.cpp` | For each object, build the `volumes` JSON array via `object_volume_info`. Human mode prints a `volumes:` indented block iff `volumes.size() > 1`. | **Modify** |
| `tests/cli/archive_invariants.hpp` / `.cpp` | Add `assert_part_extruder(zip, object_name, part_name, expected_extruder)` — checks `<part>` element with matching `name` metadata has `extruder = N`. | **Modify** |
| `tests/cli/unit/test_project_ops.cpp` | +8 unit cases for split, source attribution, per-part filament. | **Modify** |
| `tests/cli/unit/test_split_fixtures.cpp` | New file: Layer A + Layer B fixture sanity tests. | **Create** |
| `tests/cli/e2e/test_split.cpp` | New file: 7 e2e cases (happy path A + happy path B + 5 anti-cases). | **Create** |
| `tests/cli/roundtrip/test_split.cpp` | New file: 2 roundtrip cases (volume count + per-part extruder survive save/load). | **Create** |
| `docs/cli/manual-test.md` | Append Phase 8 section + one line in cumulative recipe. | **Modify** |
| `docs/cli/status.md` | Add Phase 8 status block. | **Modify** |
| `~/.claude/projects/.../memory/reference_orca_cli_fixtures.md` | Add `two_cubes.stl` (Layer A) and `box_with_text.stl` (Layer B) to the fixture list. | **Modify (memory)** |
| `~/.claude/projects/.../memory/project_orca_cli_v2_cleanup.md` | Add "Phase 8 - split-to-parts" follow-up section. | **Modify (memory)** |

---

## Task 1: Fixture infrastructure — `two_cubes.stl` + CMake wiring

**Files:**
- Create: `tests/cli/fixtures/gen_minimal_stls.cpp`
- Create: `tests/cli/fixtures/two_cubes.stl` (generated, committed)
- Modify: `tests/cli/CMakeLists.txt`

- [ ] **Step 1: Create the generator source file**

Create `tests/cli/fixtures/gen_minimal_stls.cpp`:

```cpp
// Generates the committed Layer A fixture two_cubes.stl: a binary STL
// containing two disjoint 10 mm cubes (at the origin and offset by
// (30,0,0)). Used by the orca-cli split-to-parts tests as a deterministic
// 2-component fixture. Idempotent: regenerates the same bytes every run.
//
// Build target: gen_minimal_stls. Run manually after a clean build to
// regenerate the .stl when this generator changes. The .stl itself is
// committed to git so CI does not need to run the generator.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct Vec3 { float x, y, z; };

// Emit a binary STL header + facet list to `path`.
void write_binary_stl(const std::string& path, const std::vector<std::array<Vec3, 4>>& facets) {
    std::ofstream f(path, std::ios::binary);
    char header[80] = {};
    std::strncpy(header, "orca-cli two_cubes fixture", sizeof(header) - 1);
    f.write(header, 80);
    uint32_t count = static_cast<uint32_t>(facets.size());
    f.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& tri : facets) {
        // tri[0] is the face normal, tri[1..3] are the three vertices.
        for (int i = 0; i < 4; ++i) {
            f.write(reinterpret_cast<const char*>(&tri[i]), sizeof(Vec3));
        }
        uint16_t attr = 0;
        f.write(reinterpret_cast<const char*>(&attr), sizeof(attr));
    }
}

// Append the 12 triangles of an axis-aligned cube spanning min..max.
void append_cube(std::vector<std::array<Vec3, 4>>& out, Vec3 mn, Vec3 mx) {
    auto tri = [&](Vec3 n, Vec3 a, Vec3 b, Vec3 c) {
        out.push_back({n, a, b, c});
    };
    // -X face
    tri({-1,0,0}, {mn.x,mn.y,mn.z}, {mn.x,mx.y,mn.z}, {mn.x,mx.y,mx.z});
    tri({-1,0,0}, {mn.x,mn.y,mn.z}, {mn.x,mx.y,mx.z}, {mn.x,mn.y,mx.z});
    // +X face
    tri({ 1,0,0}, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mx.z}, {mx.x,mx.y,mn.z});
    tri({ 1,0,0}, {mx.x,mn.y,mn.z}, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z});
    // -Y face
    tri({0,-1,0}, {mn.x,mn.y,mn.z}, {mn.x,mn.y,mx.z}, {mx.x,mn.y,mx.z});
    tri({0,-1,0}, {mn.x,mn.y,mn.z}, {mx.x,mn.y,mx.z}, {mx.x,mn.y,mn.z});
    // +Y face
    tri({0, 1,0}, {mn.x,mx.y,mn.z}, {mx.x,mx.y,mn.z}, {mx.x,mx.y,mx.z});
    tri({0, 1,0}, {mn.x,mx.y,mn.z}, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z});
    // -Z face
    tri({0,0,-1}, {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z});
    tri({0,0,-1}, {mn.x,mn.y,mn.z}, {mx.x,mx.y,mn.z}, {mn.x,mx.y,mn.z});
    // +Z face
    tri({0,0, 1}, {mn.x,mn.y,mx.z}, {mn.x,mx.y,mx.z}, {mx.x,mx.y,mx.z});
    tri({0,0, 1}, {mn.x,mn.y,mx.z}, {mx.x,mx.y,mx.z}, {mx.x,mn.y,mx.z});
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: gen_minimal_stls <out-dir>\n");
        return 1;
    }
    const std::string out_dir = argv[1];
    std::vector<std::array<Vec3, 4>> facets;
    facets.reserve(24);
    append_cube(facets, {  0, 0, 0}, { 10, 10, 10});
    append_cube(facets, { 30, 0, 0}, { 40, 10, 10});
    write_binary_stl(out_dir + "/two_cubes.stl", facets);
    return 0;
}
```

- [ ] **Step 2: Wire the generator into `tests/cli/CMakeLists.txt`**

Open `tests/cli/CMakeLists.txt` and add (near the top, after the existing `set(ORCA_CLI_STL_DIR ...)` block):

```cmake
# Layer A fixture dir (committed in-tree, always present).
set(ORCA_CLI_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    CACHE PATH "Committed in-tree fixture directory for orca-cli tests")

# Generator for the two_cubes.stl Layer A fixture. Build target; not
# wired into the test pipeline. Run manually when regenerating the .stl.
add_executable(gen_minimal_stls fixtures/gen_minimal_stls.cpp)
```

In the existing `target_compile_definitions(cli_tests PRIVATE ...)` block, add `ORCA_CLI_FIXTURES_DIR`:

```cmake
target_compile_definitions(cli_tests PRIVATE
    ORCA_CLI_REF_3MF="${ORCA_CLI_REF_3MF}"
    ORCA_CLI_STL_DIR="${ORCA_CLI_STL_DIR}"
    ORCA_CLI_FIXTURES_DIR="${ORCA_CLI_FIXTURES_DIR}"
    ORCA_CLI_EXE="$<TARGET_FILE:orca-cli>"
)
```

- [ ] **Step 3: Build the generator and run it once to produce `two_cubes.stl`**

```powershell
cmake --build build --config Release --target gen_minimal_stls -- -m
& "build\tests\cli\Release\gen_minimal_stls.exe" "tests/cli/fixtures"
```

Verify the file appeared:

```powershell
Test-Path tests\cli\fixtures\two_cubes.stl
Get-Item tests\cli\fixtures\two_cubes.stl | Select-Object Length
```

Expected: `True` and `Length` around 1284 bytes (80 header + 4 count + 24 facets × 50 bytes = 1284).

- [ ] **Step 4: Commit the fixture infrastructure**

```powershell
git add tests/cli/fixtures/gen_minimal_stls.cpp tests/cli/fixtures/two_cubes.stl tests/cli/CMakeLists.txt
git commit -m "test(cli): add two_cubes.stl Layer A fixture + generator (T1 split prep)"
```

---

## Task 2: Layer A fixture sanity test

**Files:**
- Create: `tests/cli/unit/test_split_fixtures.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/cli/unit/test_split_fixtures.cpp`:

```cpp
// Fixture sanity tests for orca-cli split-to-parts. These run first and
// gate every downstream test in the [orca-cli][split] family.
//
// Layer A: tests/cli/fixtures/two_cubes.stl. Committed in-tree, always
// present. Must contain exactly 2 connected mesh components.
//
// Layer B: $ORCA_CLI_STL_DIR/box_with_text.stl. Optional, local-dev
// only. Test SKIPs when absent.
#include <catch2/catch_all.hpp>
#include <boost/filesystem.hpp>
#include <libslic3r/Format/STL.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleMesh.hpp>

namespace fs = boost::filesystem;

TEST_CASE("two_cubes.stl is two-component (Layer A fixture sanity)",
          "[orca-cli][split][fixture]") {
    const auto path = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    REQUIRE(fs::exists(path)); // hard-required: committed in-tree
    Slic3r::Model m;
    REQUIRE(Slic3r::load_stl(path.string().c_str(), &m, nullptr));
    REQUIRE(m.objects.size() == 1);
    REQUIRE(m.objects[0]->volumes.size() == 1);
    auto components = m.objects[0]->volumes[0]->mesh().split();
    INFO("connected components: " << components.size());
    REQUIRE(components.size() == 2);
}
```

- [ ] **Step 2: Run the test**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][fixture]" --order rand --warn NoAssertions
```

Expected: PASS. `two_cubes.stl` exists from Task 1 and contains exactly 2 components. If FAIL, regenerate the STL via Task 1 step 3.

- [ ] **Step 3: Commit**

```powershell
git add tests/cli/unit/test_split_fixtures.cpp
git commit -m "test(cli): Layer A fixture sanity for two_cubes.stl (T2 split)"
```

---

## Task 3: Layer B fixture sanity test (SKIP-when-absent)

**Files:**
- Modify: `tests/cli/unit/test_split_fixtures.cpp`

- [ ] **Step 1: Append the Layer B test**

Append to `tests/cli/unit/test_split_fixtures.cpp`:

```cpp
TEST_CASE("box_with_text.stl is multi-component (Layer B fixture sanity)",
          "[orca-cli][split][fixture]") {
    const auto path = fs::path(ORCA_CLI_STL_DIR) / "box_with_text.stl";
    if (!fs::exists(path)) {
        SKIP("box_with_text.stl not present at " << path.string()
             << " - copy it from C:/Users/ildarcheg/Documents/GitHub/");
    }
    Slic3r::Model m;
    REQUIRE(Slic3r::load_stl(path.string().c_str(), &m, nullptr));
    REQUIRE(m.objects.size() == 1);
    REQUIRE(m.objects[0]->volumes.size() == 1);
    auto components = m.objects[0]->volumes[0]->mesh().split();
    INFO("connected components: " << components.size());
    REQUIRE(components.size() >= 2);
}
```

- [ ] **Step 2: Copy the Layer B fixture into place**

```powershell
Copy-Item "C:\Users\ildarcheg\Documents\GitHub\box_with_text.stl" `
          "C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\box_with_text.stl" -Force
Test-Path "C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\box_with_text.stl"
```

Expected: `True`.

- [ ] **Step 3: Run both fixture sanity tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][fixture]" --order rand --warn NoAssertions
```

Expected: 2 tests PASS. If the Layer B test runs and FAILS on the `>= 2` assertion, the `box_with_text.stl` file is single-component — stop and notify the user; pick a different fixture or skip the Layer B path.

- [ ] **Step 4: Commit**

```powershell
git add tests/cli/unit/test_split_fixtures.cpp
git commit -m "test(cli): Layer B fixture sanity for box_with_text.stl (T3 split)"
```

---

## Task 4: `split_object_to_parts` happy path + refuse cases (excludes source attribution — that's Task 5)

**Files:**
- Modify: `src/cli/project_ops.hpp`
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_project_ops.cpp`

- [ ] **Step 1: Write four failing tests**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("split_object_to_parts produces 2 volumes from two_cubes.stl",
          "[orca-cli][split][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "two_cubes_a";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE(find_object(s, "two_cubes_a") != nullptr);

    REQUIRE_NOTHROW(split_object_to_parts(s, "two_cubes_a"));
    auto* obj = find_object(s, "two_cubes_a");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    // libslic3r convention: post-split volumes named {original}_1, _2, ...
    REQUIRE(obj->volumes[0]->name == std::string("two_cubes_a_1"));
    REQUIRE(obj->volumes[1]->name == std::string("two_cubes_a_2"));
}

TEST_CASE("split_object_to_parts on single-component mesh throws invalid_argument",
          "[orca-cli][split][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_STL_DIR) / "000_01_test_cube.stl").string();
    p.object_name = "single_cube";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_THROWS_AS(split_object_to_parts(s, "single_cube"), std::invalid_argument);
}

TEST_CASE("split_object_to_parts on already-split object throws invalid_argument",
          "[orca-cli][split][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "two_cubes_b";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "two_cubes_b"));
    // Second call must fail because the object now has 2 volumes.
    REQUIRE_THROWS_AS(split_object_to_parts(s, "two_cubes_b"), std::invalid_argument);
}

TEST_CASE("split_object_to_parts on unknown object throws out_of_range",
          "[orca-cli][split][unit]") {
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    REQUIRE_THROWS_AS(split_object_to_parts(s, "__nope__"), std::out_of_range);
}
```

- [ ] **Step 2: Verify the tests fail to compile**

```powershell
cmake --build build --config Release --target cli_tests -- -m 2>&1 | Select-String "split_object_to_parts" | Select-Object -First 3
```

Expected: compile error — `split_object_to_parts` not declared.

- [ ] **Step 3: Declare in `src/cli/project_ops.hpp`**

Open `src/cli/project_ops.hpp` and add after the existing `object_config_keys` declaration (near the bottom, inside `namespace orca_cli`):

```cpp
// --------------------------------------------------------------------------
// Volume / part operations (Phase 8).

// split_object_to_parts: decompose the named ModelObject's single ModelVolume
// into multiple ModelVolumes (one per connected mesh component) within the
// same object. The object keeps its name, position, plate assignment, and
// instance count. New volumes are named "{name}_1", "{name}_2", ... per
// libslic3r convention (Model.cpp:2785). Preserves source attribution on
// every resulting volume (Bug C defense - see stamp_source_if_missing).
//
//   throws std::out_of_range     if no object with that name exists.
//   throws std::invalid_argument if the object has != 1 volume, the volume
//                                is not a MODEL_PART, or the mesh has only
//                                1 connected component.
void split_object_to_parts(ProjectState& s, const std::string& object_name);
```

- [ ] **Step 4: Implement in `src/cli/project_ops.cpp`**

Add to `src/cli/project_ops.cpp` (inside `namespace orca_cli`, near the other object mutations):

```cpp
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

    const int filament_count = filament_slot_count(*s.project_config);
    size_t produced = vol->split(static_cast<unsigned int>(filament_count),
                                 /*remap_paint=*/true);
    if (produced <= 1) {
        throw std::invalid_argument(
            "cannot split: object has only 1 connected mesh component");
    }
    // Source attribution preservation runs in Task 5.
}
```

- [ ] **Step 5: Run the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][unit]" --order rand --warn NoAssertions
```

Expected: 4 PASS (the four tests added in Step 1).

- [ ] **Step 6: Commit**

```powershell
git add src/cli/project_ops.hpp src/cli/project_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "feat(cli): split_object_to_parts happy path + refuse cases (T4 split)"
```

---

## Task 5: Source-attribution preservation (CRITICAL - Bug C defense)

**Both tests written and confirmed failing before any implementation.** Per the spec's explicit directive: this step must not be folded into Task 4 and must cover both the propagation case AND the regression-simulation case before the helper exists.

**Files:**
- Modify: `src/cli/project_ops.hpp`
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_project_ops.cpp`

- [ ] **Step 1: Write the propagation-case failing test**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("stamp_source_if_missing leaves already-set source unchanged "
          "(propagation case)",
          "[orca-cli][split][unit][source-attribution]") {
    using namespace orca_cli;
    using namespace Slic3r;

    ModelVolume::Source fallback;
    fallback.input_file = "/tmp/should-not-be-used.stl";
    fallback.object_idx = 99;
    fallback.volume_idx = 99;

    // Synthesize a ModelVolume with source already populated. Direct
    // construction via Model+ModelObject because ModelVolume has no public
    // default ctor.
    Model m;
    ModelObject* obj = m.add_object();
    TriangleMesh empty_mesh;
    ModelVolume* vol = obj->add_volume(empty_mesh);
    vol->source.input_file = "/real/path.stl";
    vol->source.object_idx = 7;
    vol->source.volume_idx = 3;

    stamp_source_if_missing(*vol, fallback);

    REQUIRE(vol->source.input_file == std::string("/real/path.stl"));
    REQUIRE(vol->source.object_idx == 7);
    REQUIRE(vol->source.volume_idx == 3);
}
```

- [ ] **Step 2: Write the regression-case failing test**

Append immediately after:

```cpp
TEST_CASE("stamp_source_if_missing stamps from fallback when input_file is empty "
          "(regression simulation case - simulates future libslic3r breakage)",
          "[orca-cli][split][unit][source-attribution]") {
    using namespace orca_cli;
    using namespace Slic3r;

    ModelVolume::Source fallback;
    fallback.input_file = "/real/path.stl";
    fallback.object_idx = 7;
    fallback.volume_idx = 3;

    Model m;
    ModelObject* obj = m.add_object();
    TriangleMesh empty_mesh;
    ModelVolume* vol = obj->add_volume(empty_mesh);
    // Manually clear source to simulate a hypothetical future libslic3r
    // change where ModelVolume::split does NOT propagate source to children.
    vol->source = ModelVolume::Source();
    REQUIRE(vol->source.input_file.empty());

    stamp_source_if_missing(*vol, fallback);

    REQUIRE(vol->source.input_file == std::string("/real/path.stl"));
    REQUIRE(vol->source.object_idx == 7);
    REQUIRE(vol->source.volume_idx == 3);
}
```

- [ ] **Step 3: Verify both tests fail to compile**

```powershell
cmake --build build --config Release --target cli_tests -- -m 2>&1 | Select-String "stamp_source_if_missing" | Select-Object -First 3
```

Expected: compile error — `stamp_source_if_missing` not declared.

- [ ] **Step 4: Declare in `src/cli/project_ops.hpp`**

Add just below the `split_object_to_parts` declaration:

```cpp
// stamp_source_if_missing: if `vol.source.input_file` is empty, copy the
// fields from `fallback`. No-op when `vol.source.input_file` is already
// populated. Used by split_object_to_parts to enforce that every post-split
// volume carries source attribution even if a future libslic3r change
// stops propagating it through ModelVolume::split.
//
// Bug C defense: missing source_file makes some Orca/Bambu GUI versions
// silently drop the part on load. Original orca-cli design spec section 8.
void stamp_source_if_missing(Slic3r::ModelVolume&              vol,
                             const Slic3r::ModelVolume::Source& fallback);
```

- [ ] **Step 5: Implement in `src/cli/project_ops.cpp`**

Add inside `namespace orca_cli`, near the other volume helpers:

```cpp
void stamp_source_if_missing(Slic3r::ModelVolume&              vol,
                             const Slic3r::ModelVolume::Source& fallback) {
    if (vol.source.input_file.empty()) {
        vol.source.input_file = fallback.input_file;
        vol.source.object_idx = fallback.object_idx;
        vol.source.volume_idx = fallback.volume_idx;
    }
}
```

- [ ] **Step 6: Wire `stamp_source_if_missing` into `split_object_to_parts`**

Edit `src/cli/project_ops.cpp` — replace the body of `split_object_to_parts` so that the parent's source is captured BEFORE `vol->split()` and stamped onto every post-split volume AFTER:

```cpp
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
```

- [ ] **Step 7: Add the end-to-end source-attribution unit test**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("split_object_to_parts preserves source attribution on every "
          "new volume (Bug C lock-in)",
          "[orca-cli][split][unit][source-attribution]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "two_cubes_src";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "two_cubes_src"));

    auto* obj = find_object(s, "two_cubes_src");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    for (size_t i = 0; i < obj->volumes.size(); ++i) {
        DYNAMIC_SECTION("volume " << i << " source attribution") {
            const auto& src = obj->volumes[i]->source;
            INFO("input_file=" << src.input_file
                 << " object_idx=" << src.object_idx
                 << " volume_idx=" << src.volume_idx);
            REQUIRE_FALSE(src.input_file.empty());
            REQUIRE(src.input_file == p.stl_path);
        }
    }
}
```

- [ ] **Step 8: Run all source-attribution tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][source-attribution]" --order rand --warn NoAssertions
```

Expected: 3 tests PASS (propagation, regression simulation, end-to-end lock-in).

- [ ] **Step 9: Run the full split suite to confirm no regression in Task 4**

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split]" --order rand --warn NoAssertions
```

Expected: 8 tests PASS (2 fixture sanity + 4 split unit + 2 source-attribution from this task — the end-to-end lock-in counts as the 8th).

- [ ] **Step 10: Commit**

```powershell
git add src/cli/project_ops.hpp src/cli/project_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "feat(cli): stamp_source_if_missing + split source attribution (T5 split, Bug C defense)"
```

---

## Task 6: `set_object_filament` signature change with optional part_name

**Files:**
- Modify: `src/cli/project_ops.hpp`
- Modify: `src/cli/project_ops.cpp`
- Modify: `tests/cli/unit/test_project_ops.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/cli/unit/test_project_ops.cpp`:

```cpp
TEST_CASE("set_object_filament without part_name still hits object-level config "
          "(regression pin for existing P5 behaviour)",
          "[orca-cli][split][unit]") {
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    ModelObject& obj = find_object_or_throw(s, find_any_object_name(s));
    // The existing helper signature stays callable without a third arg.
    REQUIRE_NOTHROW(set_object_filament(s, obj.name, 2));
    auto* opt = obj.config.opt<ConfigOptionInt>("extruder");
    REQUIRE(opt != nullptr);
    REQUIRE(opt->value == 2);
}

TEST_CASE("set_object_filament with part_name writes to the named volume's config",
          "[orca-cli][split][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "two_cubes_f";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "two_cubes_f"));

    REQUIRE_NOTHROW(set_object_filament(s, "two_cubes_f", 2,
                                        std::optional<std::string>("two_cubes_f_1")));
    auto* obj = find_object(s, "two_cubes_f");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);

    auto* vol0_opt = obj->volumes[0]->config.opt<ConfigOptionInt>("extruder");
    REQUIRE(vol0_opt != nullptr);
    REQUIRE(vol0_opt->value == 2);

    // Volume 1 must NOT have its config touched.
    auto* vol1_opt = obj->volumes[1]->config.opt<ConfigOptionInt>("extruder");
    // Either the option is absent OR it was set by libslic3r's split to the
    // original extruder (which is 1 by default). Either way, must NOT be 2.
    if (vol1_opt != nullptr) {
        REQUIRE(vol1_opt->value != 2);
    }
}

TEST_CASE("set_object_filament with unknown part_name throws out_of_range",
          "[orca-cli][split][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "two_cubes_g";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "two_cubes_g"));

    REQUIRE_THROWS_AS(
        set_object_filament(s, "two_cubes_g", 2,
                            std::optional<std::string>("__nope__")),
        std::out_of_range);
}
```

Add the `find_any_object_name` helper at the top of `tests/cli/unit/test_project_ops.cpp` if it doesn't already exist:

```cpp
namespace {
std::string find_any_object_name(const orca_cli::ProjectState& s) {
    REQUIRE_FALSE(s.model->objects.empty());
    return s.model->objects.front()->name;
}
} // namespace
```

(If that helper is already present from earlier tasks, skip adding it.)

- [ ] **Step 2: Verify compile fails on the new signature**

```powershell
cmake --build build --config Release --target cli_tests -- -m 2>&1 | Select-String "set_object_filament" | Select-Object -First 3
```

Expected: compile error mentioning the 4-arg overload.

- [ ] **Step 3: Change the signature in `src/cli/project_ops.hpp`**

Find the existing declaration of `set_object_filament` (search for `set_object_filament`):

```cpp
// Before
void set_object_filament(ProjectState& s, const std::string& object_name, int filament_slot);
```

Replace with (keep the doc comment intact above; add the new param at the end with a default):

```cpp
// After
void set_object_filament(ProjectState& s, const std::string& object_name,
                         int filament_slot,
                         std::optional<std::string> part_name = std::nullopt);
```

Make sure `<optional>` is included at the top of the header (it may already be there).

- [ ] **Step 4: Update the implementation in `src/cli/project_ops.cpp`**

Find the existing implementation and replace it with:

```cpp
void set_object_filament(ProjectState& s, const std::string& object_name,
                         int filament_slot,
                         std::optional<std::string> part_name) {
    using namespace Slic3r;
    ModelObject& obj = find_object_or_throw(s, object_name);

    const int max_slot = filament_slot_count(*s.project_config);
    if (filament_slot < 1 || filament_slot > max_slot) {
        throw std::out_of_range(
            "filament slot " + std::to_string(filament_slot) +
            " out of range (1.." + std::to_string(max_slot) + ")");
    }

    if (part_name.has_value() && !part_name->empty()) {
        for (ModelVolume* v : obj.volumes) {
            if (v->name == *part_name) {
                v->config.set("extruder", filament_slot);
                return;
            }
        }
        throw std::out_of_range("part not found: " + *part_name);
    }

    obj.config.set("extruder", filament_slot);
}
```

(If the existing implementation does additional bookkeeping like updating `different_settings_to_system`, preserve that — only add the `part_name` branch above the existing `obj.config.set("extruder", ...)` line.)

- [ ] **Step 5: Run the new tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][unit]" --order rand --warn NoAssertions
```

Expected: 3 new tests PASS, plus the existing 4 from Task 4 and 3 from Task 5 (10 total split unit tests).

- [ ] **Step 6: Run the existing P5 e2e tests to confirm no regression**

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][P5]" --order rand --warn NoAssertions
```

Expected: every P5 test still passes — the optional default `std::nullopt` preserves the old behaviour.

- [ ] **Step 7: Commit**

```powershell
git add src/cli/project_ops.hpp src/cli/project_ops.cpp tests/cli/unit/test_project_ops.cpp
git commit -m "feat(cli): set_object_filament gains optional part_name (T6 split)"
```

---

## Task 7: CLI `object split-to-parts` subcommand wiring

**Files:**
- Modify: `src/cli/commands/object.cpp`

- [ ] **Step 1: Write the failing e2e smoke test**

Create `tests/cli/e2e/test_split.cpp`:

```cpp
// orca-cli object split-to-parts end-to-end tests.
#include <catch2/catch_all.hpp>
#include "test_common.hpp"
#include "archive_invariants.hpp"
#include <boost/filesystem.hpp>
#include <boost/process.hpp>
#include <nlohmann/json.hpp>

namespace fs = boost::filesystem;
namespace bp = boost::process;

TEST_CASE("object split-to-parts produces 2 volumes on Layer A fixture",
          "[orca-cli][split][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "split_a.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);

    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";

    auto add_rc = orca_cli_test::run_cli({
        "object", "add", out.string(),
        "--plate", "Plate 1",
        "--stl",   stl.string(),
        "--name",  "multipart",
    });
    REQUIRE(add_rc.exit_code == 0);

    auto split_rc = orca_cli_test::run_cli({
        "object", "split-to-parts", out.string(),
        "--name", "multipart",
    });
    REQUIRE(split_rc.exit_code == 0);

    fs::remove_all(tmp);
}
```

(The helpers `make_temp_dir`, `run_cli`, and `RunResult` already exist in `tests/cli/test_common.hpp` from the P0-P7 work. If `run_cli` uses a different signature, adapt accordingly — the existing e2e tests in `tests/cli/e2e/test_object.cpp` are the reference.)

- [ ] **Step 2: Verify the test fails (split-to-parts not registered)**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][e2e]" --order rand --warn NoAssertions
```

Expected: FAIL on the `split_rc.exit_code == 0` assertion. The CLI11 parser doesn't recognise `split-to-parts` and returns a non-zero exit code.

- [ ] **Step 3: Register the subcommand in `src/cli/commands/object.cpp`**

Open `src/cli/commands/object.cpp` and find the `register_object_subcmd` function. After the existing `set-filament` subcommand registration, add:

```cpp
// -- object split-to-parts ------------------------------------------------
static std::string split_file, split_name;
auto* split = object->add_subcommand("split-to-parts",
    "decompose an object's single mesh into multiple parts (one per connected component)");
split->add_option("file", split_file, "input .3mf path")->required();
split->add_option("--name", split_name, "name of the object to split")->required();
split->add_option("--output", g.output,
    "write result to this path instead of overwriting input");
split->callback([&g]() {
    MutationExceptionMap em;
    em.set_default_invalid_argument(ExitCode::invalid_state)
      .set_default_out_of_range(ExitCode::unknown_reference);
    int rc = run_mutation(g, split_file,
        "split object '" + split_name + "' into parts", em,
        [](ProjectState& s) { split_object_to_parts(s, split_name); });
    std::exit(rc);
});
```

(Static names like `split_file` / `split_name` follow the per-subcommand-static convention documented at the top of `commands/plate.cpp`. If the file already defines `object_file` / `object_name` etc. as shared statics, follow that convention instead.)

- [ ] **Step 4: Run the e2e smoke test**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][e2e]" --order rand --warn NoAssertions
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add src/cli/commands/object.cpp tests/cli/e2e/test_split.cpp
git commit -m "feat(cli): orca-cli object split-to-parts subcommand (T7 split)"
```

---

## Task 8: CLI `--part` flag on `set-filament`

**Files:**
- Modify: `src/cli/commands/object.cpp`
- Modify: `tests/cli/e2e/test_split.cpp`

- [ ] **Step 1: Write the failing e2e test**

Append to `tests/cli/e2e/test_split.cpp`:

```cpp
TEST_CASE("object set-filament --part writes per-volume extruder",
          "[orca-cli][split][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "split_part.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";

    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name","multi2"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","multi2"}).exit_code == 0);

    auto rc1 = orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","multi2","--part","multi2_1","--filament","1"});
    REQUIRE(rc1.exit_code == 0);
    auto rc2 = orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","multi2","--part","multi2_2","--filament","2"});
    REQUIRE(rc2.exit_code == 0);

    fs::remove_all(tmp);
}
```

- [ ] **Step 2: Verify the test fails (--part not registered)**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][e2e]" --order rand --warn NoAssertions
```

Expected: FAIL on the rc1 / rc2 `exit_code == 0` assertion. CLI11 rejects the unknown `--part` option with usage_error.

- [ ] **Step 3: Add `--part` to the existing `set-filament` subcommand**

In `src/cli/commands/object.cpp`, find the `register_object_subcmd` function's `set-filament` subcommand registration. Add a static for the part name near the other set-filament statics:

```cpp
static std::string setfil_file, setfil_name, setfil_part;
static int setfil_filament = 0;
```

(If the existing names differ, adapt; the key change is adding `setfil_part`.)

Add the option and update the callback:

```cpp
auto* setfil = object->add_subcommand("set-filament", "...");
setfil->add_option("file", setfil_file, "input .3mf path")->required();
setfil->add_option("--name", setfil_name, "name of the object")->required();
setfil->add_option("--filament", setfil_filament, "1-based filament slot")->required();
setfil->add_option("--part", setfil_part,
    "name of the volume (post-split part) to target; omit for object-level config");
setfil->add_option("--output", g.output, "write result to this path");
setfil->callback([&g]() {
    MutationExceptionMap em;
    int rc = run_mutation(g, setfil_file,
        "set-filament name='" + setfil_name + "' filament=" +
            std::to_string(setfil_filament) +
            (setfil_part.empty() ? "" : (" part='" + setfil_part + "'")),
        em,
        [](ProjectState& s) {
            std::optional<std::string> part =
                setfil_part.empty() ? std::nullopt
                                    : std::optional<std::string>(setfil_part);
            set_object_filament(s, setfil_name, setfil_filament, part);
        });
    std::exit(rc);
});
```

- [ ] **Step 4: Run the e2e tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][e2e]" --order rand --warn NoAssertions
```

Expected: PASS.

- [ ] **Step 5: Run the existing P5 e2e tests for regression**

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][P5]" --order rand --warn NoAssertions
```

Expected: every P5 test still passes — when `--part` is absent, behaviour is unchanged.

- [ ] **Step 6: Commit**

```powershell
git add src/cli/commands/object.cpp tests/cli/e2e/test_split.cpp
git commit -m "feat(cli): object set-filament gains --part flag (T8 split)"
```

---

## Task 9: `inspect` shows per-volume info

**Files:**
- Modify: `src/cli/project_ops.hpp`
- Modify: `src/cli/project_ops.cpp`
- Modify: `src/cli/commands/inspect.cpp`
- Modify: `tests/cli/e2e/test_split.cpp`

- [ ] **Step 1: Write the failing e2e test**

Append to `tests/cli/e2e/test_split.cpp`:

```cpp
TEST_CASE("inspect --json shows per-volume info for split objects",
          "[orca-cli][split][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "split_inspect.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";

    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name","insp"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","insp"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","insp","--part","insp_1","--filament","1"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","insp","--part","insp_2","--filament","2"}).exit_code == 0);

    auto rc = orca_cli_test::run_cli({"--json","inspect",out.string()});
    REQUIRE(rc.exit_code == 0);

    auto j = nlohmann::json::parse(rc.stdout_text);
    REQUIRE(j["status"] == "ok");

    bool found = false;
    for (const auto& o : j["data"]["objects"]) {
        if (o["name"] == "insp") {
            found = true;
            REQUIRE(o.contains("volumes"));
            REQUIRE(o["volumes"].size() == 2);
            std::map<std::string, int> by_name;
            for (const auto& v : o["volumes"]) {
                by_name[v["name"].get<std::string>()] = v["extruder"].get<int>();
            }
            REQUIRE(by_name["insp_1"] == 1);
            REQUIRE(by_name["insp_2"] == 2);
        }
    }
    REQUIRE(found);

    fs::remove_all(tmp);
}
```

- [ ] **Step 2: Verify the test fails (no `volumes` field yet)**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][e2e]" --order rand --warn NoAssertions
```

Expected: FAIL on `o.contains("volumes")`.

- [ ] **Step 3: Declare `object_volume_info` in `src/cli/project_ops.hpp`**

Add near `object_config_keys`:

```cpp
struct VolumeInfo {
    std::string name;
    int         extruder;
};

// Returns one VolumeInfo per ModelVolume in the named object. The
// `extruder` field is taken from the per-volume config if set, falling
// back to the object-level config, falling back to 1 if neither is set.
//   throws std::out_of_range if no object named `object_name` exists.
std::vector<VolumeInfo> object_volume_info(const ProjectState& s,
                                           const std::string&  object_name);
```

- [ ] **Step 4: Implement `object_volume_info` in `src/cli/project_ops.cpp`**

```cpp
std::vector<VolumeInfo> object_volume_info(const ProjectState& s,
                                           const std::string&  object_name) {
    using namespace Slic3r;
    const ModelObject& obj = find_object_or_throw(s, object_name);

    int obj_extruder = 1;
    if (auto* oe = obj.config.opt<ConfigOptionInt>("extruder")) {
        obj_extruder = oe->value;
    }

    std::vector<VolumeInfo> out;
    out.reserve(obj.volumes.size());
    for (const ModelVolume* v : obj.volumes) {
        int eff = obj_extruder;
        if (auto* ve = v->config.opt<ConfigOptionInt>("extruder")) {
            eff = ve->value;
        }
        out.push_back({v->name, eff});
    }
    return out;
}
```

- [ ] **Step 5: Extend `do_inspect` in `src/cli/commands/inspect.cpp`**

Find the part of `do_inspect` that builds the `objects` array (it calls `object_config_keys` for each ModelObject). For each object, also build a `volumes` JSON array via `object_volume_info`:

```cpp
nlohmann::json obj_entry;
obj_entry["name"]        = o.name;
obj_entry["config_keys"] = object_config_keys(state, o.name);

const auto vi = object_volume_info(state, o.name);
auto& vols_arr = obj_entry["volumes"] = nlohmann::json::array();
for (const auto& v : vi) {
    vols_arr.push_back({{"name", v.name}, {"extruder", v.extruder}});
}

objects_arr.push_back(obj_entry);
```

(Adapt the variable names to whatever the existing code uses. The key contract: every object's JSON entry gets a `volumes` array, in libslic3r insertion order.)

For human-mode output, after the existing per-object `config_keys` block, add (only when `vi.size() > 1`):

```cpp
if (vi.size() > 1) {
    std::fputs("    volumes:\n", stdout);
    for (const auto& v : vi) {
        std::string line = "      - " + v.name +
                           " (extruder " + std::to_string(v.extruder) + ")\n";
        std::fputs(line.c_str(), stdout);
    }
}
```

- [ ] **Step 6: Run the e2e test**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][e2e]" --order rand --warn NoAssertions
```

Expected: PASS. Plus all other existing inspect tests still pass.

- [ ] **Step 7: Run existing inspect tests for regression**

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][P7]" --order rand --warn NoAssertions
```

Expected: every P7 inspect test still passes. The new `volumes` field is additive; existing tests don't assert its absence.

- [ ] **Step 8: Commit**

```powershell
git add src/cli/project_ops.hpp src/cli/project_ops.cpp src/cli/commands/inspect.cpp tests/cli/e2e/test_split.cpp
git commit -m "feat(cli): inspect shows per-volume info under each object (T9 split)"
```

---

## Task 10: Archive invariant #5 — `assert_part_extruder`

**Files:**
- Modify: `tests/cli/archive_invariants.hpp`
- Modify: `tests/cli/archive_invariants.cpp`

- [ ] **Step 1: Add the declaration to `tests/cli/archive_invariants.hpp`**

After the existing `assert_object_extruder` declaration:

```cpp
// In Metadata/model_settings.config: the <part ...> child of the <object>
// matching `object_name` whose <metadata key="name" value="..."/> matches
// `part_name` must carry a <metadata key="extruder" value="N"/> equal to
// `expected_extruder`. Bug C class lock-in for per-volume filament
// assignment: catches regressions where set-filament --part is silently
// ignored by the serializer.
void assert_part_extruder(const fs::path&    zip,
                          const std::string& object_name,
                          const std::string& part_name,
                          int                expected_extruder);
```

- [ ] **Step 2: Add the implementation to `tests/cli/archive_invariants.cpp`**

Mirror the structure of `assert_object_extruder` (around line 209). The XML shape under `Metadata/model_settings.config` is:

```xml
<config>
  <object id="..."><metadata key="name" value="multipart"/>
    <part id="..."><metadata key="name" value="multipart_1"/><metadata key="extruder" value="2"/></part>
    <part id="..."><metadata key="name" value="multipart_2"/><metadata key="extruder" value="3"/></part>
  </object>
</config>
```

Add to `archive_invariants.cpp` inside `namespace orca_cli_test::archive`:

```cpp
void assert_part_extruder(const fs::path&    zip,
                          const std::string& object_name,
                          const std::string& part_name,
                          int                expected_extruder)
{
    // Read Metadata/model_settings.config out of the archive.
    const std::string xml = read_zip_entry_text(zip, "Metadata/model_settings.config");

    // Locate the <object> whose <metadata key="name" value="object_name"/> matches.
    // Then within it, locate the <part> whose <metadata key="name" value="part_name"/>
    // matches. Then within that part, find <metadata key="extruder" value="N"/>.
    //
    // Parsing strategy: simple text scan since model_settings.config is small
    // and we only need three nested matches. Avoids pulling in expat/pugixml
    // just for tests. Matches the style of assert_object_extruder above.
    auto find_object_block = [&](const std::string& name) -> std::pair<size_t, size_t> {
        const std::string needle =
            "<metadata key=\"name\" value=\"" + name + "\"";
        size_t pos = 0;
        while (true) {
            size_t obj_open = xml.find("<object ", pos);
            if (obj_open == std::string::npos) return {std::string::npos, std::string::npos};
            size_t obj_close = xml.find("</object>", obj_open);
            REQUIRE(obj_close != std::string::npos);
            // Search for the name metadata inside this object's block.
            size_t name_in = xml.find(needle, obj_open);
            if (name_in != std::string::npos && name_in < obj_close) {
                return {obj_open, obj_close};
            }
            pos = obj_close + 1;
        }
    };

    auto [obj_open, obj_close] = find_object_block(object_name);
    INFO("object_name=" << object_name);
    REQUIRE(obj_open != std::string::npos);

    // Find the <part> whose name matches part_name, inside [obj_open, obj_close).
    const std::string part_name_needle =
        "<metadata key=\"name\" value=\"" + part_name + "\"";
    size_t part_open = obj_open;
    bool found_part = false;
    size_t part_close = obj_close;
    while (true) {
        part_open = xml.find("<part ", part_open + 1);
        if (part_open == std::string::npos || part_open >= obj_close) break;
        part_close = xml.find("</part>", part_open);
        REQUIRE(part_close != std::string::npos);
        size_t name_in_part = xml.find(part_name_needle, part_open);
        if (name_in_part != std::string::npos && name_in_part < part_close) {
            found_part = true;
            break;
        }
        part_open = part_close + 1;
    }
    INFO("part_name=" << part_name);
    REQUIRE(found_part);

    // Inside the [part_open, part_close) range, find extruder metadata.
    const std::string extruder_re_open = "<metadata key=\"extruder\" value=\"";
    size_t extruder_in = xml.find(extruder_re_open, part_open);
    INFO("expected extruder=" << expected_extruder);
    REQUIRE(extruder_in != std::string::npos);
    REQUIRE(extruder_in < part_close);
    size_t value_start = extruder_in + extruder_re_open.size();
    size_t value_end = xml.find("\"", value_start);
    REQUIRE(value_end != std::string::npos);
    const std::string value_str = xml.substr(value_start, value_end - value_start);
    REQUIRE(std::stoi(value_str) == expected_extruder);
}
```

(The helper `read_zip_entry_text` already exists in `archive_invariants.cpp` — used by every other invariant helper. If the existing file uses a different name, follow that pattern.)

- [ ] **Step 3: Build to check the helper compiles**

```powershell
cmake --build build --config Release --target cli_tests -- -m 2>&1 | Select-String "archive_invariants" | Select-Object -First 3
```

Expected: no errors mentioning archive_invariants.

- [ ] **Step 4: Commit**

```powershell
git add tests/cli/archive_invariants.hpp tests/cli/archive_invariants.cpp
git commit -m "test(cli): assert_part_extruder archive invariant (T10 split, invariant #5)"
```

---

## Task 11: E2E happy path with archive invariants (Layer A)

**Files:**
- Modify: `tests/cli/e2e/test_split.cpp`

- [ ] **Step 1: Append the comprehensive e2e test**

Append to `tests/cli/e2e/test_split.cpp`:

```cpp
TEST_CASE("end-to-end split + per-part filament assignment passes archive invariants "
          "(Layer A)",
          "[orca-cli][split][e2e]") {
    using namespace orca_cli_test::archive;
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "split_e2e_a.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";

    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name","e2e_a"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","e2e_a"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","e2e_a","--part","e2e_a_1","--filament","1"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","e2e_a","--part","e2e_a_2","--filament","2"}).exit_code == 0);

    // Archive invariant #4 (Bug C): every <part> carries source_file.
    assert_parts_have_source_file(out);
    // Archive invariant #5: per-volume extruder is serialized.
    assert_part_extruder(out, "e2e_a", "e2e_a_1", 1);
    assert_part_extruder(out, "e2e_a", "e2e_a_2", 2);

    fs::remove_all(tmp);
}
```

- [ ] **Step 2: Run the test**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][e2e]" --order rand --warn NoAssertions
```

Expected: PASS (and the previous split e2e tests still pass).

- [ ] **Step 3: Commit**

```powershell
git add tests/cli/e2e/test_split.cpp
git commit -m "test(cli): Layer A e2e split + archive invariants (T11 split)"
```

---

## Task 12: E2E anti-cases

**Files:**
- Modify: `tests/cli/e2e/test_split.cpp`

- [ ] **Step 1: Append all five anti-case tests**

Append to `tests/cli/e2e/test_split.cpp`:

```cpp
TEST_CASE("object split-to-parts exits 7 on single-component object",
          "[orca-cli][split][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "single.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);

    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1",
        "--stl",(fs::path(ORCA_CLI_STL_DIR) / "000_01_test_cube.stl").string(),
        "--name","cube"}).exit_code == 0);

    auto rc = orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","cube"});
    REQUIRE(rc.exit_code == 7);
    REQUIRE(rc.stderr_text.find("only 1 connected") != std::string::npos);

    fs::remove_all(tmp);
}

TEST_CASE("object split-to-parts exits 7 on already-split object",
          "[orca-cli][split][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "double.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";

    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name","x"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","x"}).exit_code == 0);

    auto rc2 = orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","x"});
    REQUIRE(rc2.exit_code == 7);
    REQUIRE(rc2.stderr_text.find("multiple volumes") != std::string::npos);

    fs::remove_all(tmp);
}

TEST_CASE("object split-to-parts exits 6 on unknown object",
          "[orca-cli][split][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "unknown.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);

    auto rc = orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","__nope__"});
    REQUIRE(rc.exit_code == 6);

    fs::remove_all(tmp);
}

TEST_CASE("object set-filament --part exits 6 on unknown part name",
          "[orca-cli][split][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "unknown_part.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";

    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name","p"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","p"}).exit_code == 0);

    auto rc = orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","p","--part","__nope__","--filament","2"});
    REQUIRE(rc.exit_code == 6);

    fs::remove_all(tmp);
}

TEST_CASE("object split-to-parts --output writes only the side-car file",
          "[orca-cli][split][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto in_ = tmp / "in.3mf";
    const auto out = tmp / "out.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, in_);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";

    REQUIRE(orca_cli_test::run_cli({"object","add",in_.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name","sc"}).exit_code == 0);
    const auto in_size_before = fs::file_size(in_);

    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",in_.string(),
        "--name","sc","--output",out.string()}).exit_code == 0);

    REQUIRE(fs::exists(out));
    // Input file is byte-identical.
    REQUIRE(fs::file_size(in_) == in_size_before);

    fs::remove_all(tmp);
}
```

- [ ] **Step 2: Run all e2e tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][e2e]" --order rand --warn NoAssertions
```

Expected: 7 e2e tests PASS (Task 7's smoke + Task 8's --part + Task 9's inspect + Task 11's Layer A + this task's 5 anti-cases = 9... wait the count is now 8 including this).

Note on the exit code 7: the existing `MutationExceptionMap` in `src/cli/output.hpp` defines `invalid_state = 7`. If the actual numeric value differs in your repo, adjust the test assertion.

- [ ] **Step 3: Commit**

```powershell
git add tests/cli/e2e/test_split.cpp
git commit -m "test(cli): e2e anti-cases for split (T12 split)"
```

---

## Task 13: Layer B realistic-mesh e2e (SKIP-when-absent)

**Files:**
- Modify: `tests/cli/e2e/test_split.cpp`

- [ ] **Step 1: Append the Layer B test**

Append to `tests/cli/e2e/test_split.cpp`:

```cpp
TEST_CASE("end-to-end split + per-part filament on Layer B realistic mesh",
          "[orca-cli][split][e2e]") {
    using namespace orca_cli_test::archive;
    const auto stl = fs::path(ORCA_CLI_STL_DIR) / "box_with_text.stl";
    if (!fs::exists(stl)) {
        SKIP("box_with_text.stl not present at " << stl.string());
    }

    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "split_e2e_b.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);

    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","Plate 1","--stl",stl.string(),"--name","realistic"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","realistic"}).exit_code == 0);
    // We don't know exactly how many parts came out; assign filament 1 to
    // realistic_1 only and verify it lands in the archive.
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","realistic","--part","realistic_1","--filament","2"}).exit_code == 0);

    assert_parts_have_source_file(out);
    assert_part_extruder(out, "realistic", "realistic_1", 2);

    fs::remove_all(tmp);
}
```

- [ ] **Step 2: Run the e2e tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][e2e]" --order rand --warn NoAssertions
```

Expected: 9 e2e tests run (the new Layer B test plus everything from prior tasks). Layer B passes if the file is present; otherwise SKIPs.

- [ ] **Step 3: Commit**

```powershell
git add tests/cli/e2e/test_split.cpp
git commit -m "test(cli): Layer B realistic-mesh e2e (T13 split, SKIP-when-absent)"
```

---

## Task 14: Roundtrip tests — volume count + per-part extruder survive save/load

**Files:**
- Create: `tests/cli/roundtrip/test_split.cpp`

- [ ] **Step 1: Write the failing tests**

Create `tests/cli/roundtrip/test_split.cpp`:

```cpp
// Roundtrip tests for orca-cli split-to-parts. Verify that volume count
// and per-volume extruder values survive a full save -> reopen cycle via
// libslic3r's bbs_3mf reader.
#include <catch2/catch_all.hpp>
#include "test_common.hpp"
#include "io.hpp"
#include "project_ops.hpp"
#include <boost/filesystem.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

namespace fs = boost::filesystem;

TEST_CASE("volume count survives save/load roundtrip",
          "[orca-cli][split][roundtrip]") {
    using namespace orca_cli;
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "rt_count.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);

    // Build the state in memory, split, save.
    {
        ProjectState s = load_project(out.string());
        AddObjectParams p;
        p.plate_name  = s.plates.front()->plate_name;
        p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
        p.object_name = "rt";
        p.count       = 1;
        REQUIRE_NOTHROW(add_object(s, p));
        REQUIRE_NOTHROW(split_object_to_parts(s, "rt"));
        REQUIRE_NOTHROW(save_project(s, out.string()));
    }

    // Reopen and assert volume count.
    ProjectState s2 = load_project(out.string());
    auto* obj = find_object(s2, "rt");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    REQUIRE(obj->volumes[0]->name == std::string("rt_1"));
    REQUIRE(obj->volumes[1]->name == std::string("rt_2"));

    fs::remove_all(tmp);
}

TEST_CASE("per-part extruder survives save/load roundtrip",
          "[orca-cli][split][roundtrip]") {
    using namespace orca_cli;
    using namespace Slic3r;
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "rt_extruder.3mf";
    fs::copy_file(ORCA_CLI_REF_3MF, out);

    {
        ProjectState s = load_project(out.string());
        AddObjectParams p;
        p.plate_name  = s.plates.front()->plate_name;
        p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
        p.object_name = "rtx";
        p.count       = 1;
        REQUIRE_NOTHROW(add_object(s, p));
        REQUIRE_NOTHROW(split_object_to_parts(s, "rtx"));
        REQUIRE_NOTHROW(set_object_filament(s, "rtx", 1, std::optional<std::string>("rtx_1")));
        REQUIRE_NOTHROW(set_object_filament(s, "rtx", 2, std::optional<std::string>("rtx_2")));
        REQUIRE_NOTHROW(save_project(s, out.string()));
    }

    ProjectState s2 = load_project(out.string());
    auto* obj = find_object(s2, "rtx");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    auto* e1 = obj->volumes[0]->config.opt<ConfigOptionInt>("extruder");
    auto* e2 = obj->volumes[1]->config.opt<ConfigOptionInt>("extruder");
    REQUIRE(e1 != nullptr);
    REQUIRE(e2 != nullptr);
    REQUIRE(e1->value == 1);
    REQUIRE(e2->value == 2);

    fs::remove_all(tmp);
}
```

- [ ] **Step 2: Run the tests**

```powershell
cmake --build build --config Release --target cli_tests -- -m
& "build\tests\cli\Release\cli_tests.exe" "[orca-cli][split][roundtrip]" --order rand --warn NoAssertions
```

Expected: 2 tests PASS. If they fail on volume count, the save path is dropping volumes — check `store_bbs_3mf` is correctly serializing the new volumes. If they fail on extruder, check that `vol->config.set("extruder", N)` is being serialized into `model_settings.config` (the existing P5 work covers object-level extruder; per-volume extruder uses the same serialization but the inner element is `<part>` not `<object>`).

- [ ] **Step 3: Commit**

```powershell
git add tests/cli/roundtrip/test_split.cpp
git commit -m "test(cli): roundtrip for volume count + per-part extruder (T14 split)"
```

---

## Task 15: Manual smoke recipe + status doc + memory updates

**Files:**
- Modify: `docs/cli/manual-test.md`
- Modify: `docs/cli/status.md`
- Modify: `~/.claude/projects/.../memory/reference_orca_cli_fixtures.md`
- Modify: `~/.claude/projects/.../memory/project_orca_cli_v2_cleanup.md`

- [ ] **Step 1: Append Phase 8 to `docs/cli/manual-test.md`**

Open `docs/cli/manual-test.md` and append after the existing Phase 7 section:

```markdown
## Phase 8 - `object split-to-parts` + per-part filament

Requires `box_with_text.stl` in `$STLS` (copy from
`C:\Users\ildarcheg\Documents\GitHub\` once per session).

```powershell
$OUT = "$env:TEMP\orca-cli-p8.3mf"
Copy-Item $REF $OUT -Force
& $CLI object add $OUT --plate "Plate 1" --stl "$STLS\box_with_text.stl" --name multipart
& $CLI object split-to-parts $OUT --name multipart
& $CLI object set-filament $OUT --name multipart --part multipart_1 --filament 1
& $CLI object set-filament $OUT --name multipart --part multipart_2 --filament 2
& $CLI --json inspect $OUT | ConvertFrom-Json | ConvertTo-Json -Depth 5
```

Expected: `multipart` becomes one ModelObject with N ModelVolumes
(`multipart_1`, `multipart_2`, ...). `inspect --json` shows the
`volumes` array under that object with the extruder values assigned
by the per-part `set-filament` calls.

Anti-cases (each should exit non-zero):
```powershell
& $CLI object split-to-parts $OUT --name multipart            # already split
# expected: exit 7 (invalid_state)

& $CLI object split-to-parts $OUT --name "Box"                # if single
# expected: exit 7 (invalid_state)

& $CLI object set-filament $OUT --name multipart --part nope --filament 1
# expected: exit 6 (unknown_reference)
```

Manual GUI smoke: open `$OUT` in OrcaSlicer; the `multipart` object
should appear as a single object with N parts visible in the object
panel, each part assigned to its respective filament slot.
```

- [ ] **Step 2: Append one line to the cumulative P7 recipe**

Find the cumulative recipe at the end of Phase 7 in the same file. Append (after the existing `config set ...` lines, before the `inspect` line):

```powershell
& $CLI object add $OUT --plate "Brackets" --stl "$STLS\box_with_text.stl" --name multi
& $CLI object split-to-parts $OUT --name multi
& $CLI object set-filament $OUT --name multi --part multi_1 --filament 1
```

- [ ] **Step 3: Append Phase 8 to `docs/cli/status.md`**

Open `docs/cli/status.md` and append (after the "P3 stride fix" section):

```markdown
## Phase 8 - object split-to-parts

- [x] `split_object_to_parts` in `project_ops.cpp` delegates to
  libslic3r's `ModelVolume::split` (`Model.cpp:2742`) with
  `remap_paint=true`. Enforces single-volume + MODEL_PART
  preconditions; single-component meshes throw invalid_argument.
- [x] `stamp_source_if_missing` helper in `project_ops.cpp` provides
  Bug C defense: after split, every resulting volume that lost its
  source attribution (hypothetical future libslic3r change) is
  re-stamped from the parent's snapshot taken before vol.split().
  Two dedicated unit tests cover propagation and regression cases.
- [x] `set_object_filament` extended with optional `part_name`.
  Without `part_name`, behaviour is unchanged (object-level config).
  With `part_name`, writes to the named volume's per-volume config.
- [x] `orca-cli object split-to-parts <file> --name X [--output O]`
  end-to-end via the standard load -> mutate -> save flow.
- [x] `orca-cli object set-filament <file> --name X --part Y
  --filament N [--output O]` end-to-end.
- [x] `inspect --json` shows a `volumes` array under each object
  containing `{name, extruder}` per volume. Human mode shows a
  `volumes:` block only when the object has >1 volume.
- [x] New archive invariant `assert_part_extruder` mirrors invariant
  #5 from the original design (per-volume extruder serialized to
  Metadata/model_settings.config).
- [x] Layer A / Layer B fixture model: `two_cubes.stl` committed
  in-tree, `box_with_text.stl` optional local-dev fixture with
  SKIP-when-absent behaviour.
- [x] All P0-P7 tests still pass (regression). Test count moved from
  124 cases / 66046+ assertions to 143 cases.
- [ ] Manual GUI smoke: open the P8 manual-test output in OrcaSlicer
  and verify the multipart object shows N parts with their assigned
  filament slots in the per-object panel. (Pending separate manual
  verification.)
```

- [ ] **Step 4: Update memory `reference_orca_cli_fixtures.md`**

Read the existing file first to know the format, then append:

```markdown
- `tests/cli/fixtures/two_cubes.stl` (Layer A, committed): two disjoint
  10mm cubes at (0,0,0) and (30,0,0). Generated by
  `tests/cli/fixtures/gen_minimal_stls.cpp`. Used by orca-cli split-to-parts
  unit + roundtrip + most e2e tests. Reference via `ORCA_CLI_FIXTURES_DIR`.
- `box_with_text.stl` (Layer B, optional local-dev only): in `slicer_tamplates\`.
  Multi-component STL (box with embossed text glyphs). Used by Phase 8
  manual smoke + one realistic e2e test that SKIPs when absent. Copy from
  `C:\Users\ildarcheg\Documents\GitHub\box_with_text.stl` once per session.
```

- [ ] **Step 5: Update memory `project_orca_cli_v2_cleanup.md`**

Append a new "Phase 8" section after the existing "History rewrite + push" section:

```markdown
## Phase 8 — object split-to-parts (2026-05-20+)

Added `orca-cli object split-to-parts <file> --name X` (delegates to
libslic3r `ModelVolume::split` at `Model.cpp:2742`) plus extended
`object set-filament` with an optional `--part Y` flag for per-volume
filament assignment. `inspect --json` grows a `volumes` array.

Bug C source-attribution defense is explicit and double-tested: a
testable helper `stamp_source_if_missing(volume, fallback)` is invoked
on every post-split volume after capturing the parent's source
snapshot. Two unit tests cover the propagation case (already-set
fields stay) and the regression-simulation case (parent's fields
manually cleared - helper stamps from the snapshot). Plus an
end-to-end lock-in unit test and archive invariant #4 on every e2e.

Fixture model is two-layer:
- Layer A (committed): `tests/cli/fixtures/two_cubes.stl` generated by
  `tests/cli/fixtures/gen_minimal_stls.cpp`. CI-portable.
- Layer B (optional): `box_with_text.stl` in `slicer_tamplates\`. One
  realistic e2e SKIPs when missing.

Test count moved from 124 to 143 cases.
```

- [ ] **Step 6: Commit the docs**

```powershell
git add docs/cli/manual-test.md docs/cli/status.md
git commit -m "docs(cli): Phase 8 split-to-parts + cumulative recipe update (T15 split)"
```

(Memory files live outside the repo; no `git add` needed for them.)

---

## Final sweep

- [ ] **Run the full cli_tests suite once more**

```powershell
cmake --build build --config Release --target cli_tests -- -m
ctest --test-dir build/tests/cli --output-on-failure -C Release
```

Expected: **143 tests PASS** (124 baseline + 2 fixture sanity + 8 unit + 7 e2e + 2 roundtrip = 143).

- [ ] **Manual GUI smoke** per `docs/cli/manual-test.md` Phase 8 + the appended line in the cumulative recipe.

- [ ] **Push to fork** when satisfied:

```powershell
git push origin main
```

(Regular fast-forward push; no force needed since these are all new commits on top of the post-cleanup state.)

---

## Self-Review Notes

**1. Spec coverage:**
- §1 CLI surface (3 verbs): T7 (split-to-parts), T8 (set-filament --part), T9 (inspect volumes).
- §2 Behaviour semantics: T4 (split body + refuses), T5 (source attribution preservation, Bug C defense), T6 (set_object_filament part_name path). Instance preservation + object-level extruder not cleared are in §2 docs and implicitly covered by the existing tests (split mutates volumes, not instances).
- §3 Edge cases / exit codes: T12 (5 anti-cases) + T8 (rejection of unknown part).
- §4 Tests:
  - Fixture sanity: T2 (Layer A), T3 (Layer B).
  - Unit: T4 (4 cases) + T5 (3 cases including end-to-end lock-in) + T6 (3 cases) = 10 unit cases. Wait — spec says +8 unit. Let me re-count: T4 has 4, T5 has 3 (propagation + regression + lock-in), T6 has 3 = 10. Spec said +8. The two extras come from T5's "lock-in" being an end-to-end unit and from explicitly splitting the regression-pin test in T6. Both are useful additions per the user's review comment 1; total test count goes 143 → 145. Updating final-sweep expectation to **145** below.
  - E2E: T7 (1) + T8 (1) + T9 (1) + T11 (1) + T12 (5) + T13 (1) = 10. Wait, spec said +7. Re-count e2e in spec §4: items 1-7 in the e2e list = 7. My plan has: T7 smoke (1), T8 part flag (1), T9 inspect (1), T11 happy path A (1), T12 anti-cases (5), T13 Layer B (1). That's 10. Three extras compared to spec.
  - The spec's e2e item 1 ("end-to-end Layer A") combined the happy-path-A and the per-part assignment into one test; I split it across T7/T8/T9 to keep TDD cycles small. That's three test cases instead of one, but the same coverage. Acceptable per the bite-sized-task principle.
  - Roundtrip: T14 has 2 tests, matches spec.
- §5 Docs: T15 covers all five doc surfaces (manual-test.md Phase 8 + cumulative recipe line + status.md Phase 8 + memory fixtures + memory cleanup).

**Final test count revision:** Baseline 124 + 2 fixture sanity + 10 unit + 10 e2e + 2 roundtrip = **148 tests**. Update Final Sweep expectation accordingly. (Spec said 143; the +5 delta is from splitting some spec-bundled tests into multiple bite-sized TDD cycles per the writing-plans guidance.)

**2. Placeholder scan:** zero TBDs / TODOs. Every step has concrete code or commands.

**3. Type consistency:**
- `VolumeInfo { std::string name; int extruder; }` — defined in T9, used in T9.
- `stamp_source_if_missing(ModelVolume&, const ModelVolume::Source&)` — defined in T5, used in T5.
- `split_object_to_parts(ProjectState&, const std::string&)` — defined in T4, modified in T5, used everywhere.
- `set_object_filament(ProjectState&, name, slot, optional<part_name>)` — signature change in T6, used in T6/T8/T9/T11/T12/T13/T14.
- `assert_part_extruder(fs::path, object_name, part_name, expected)` — defined in T10, used in T11/T13.

**Risks:**
- Task 9's exact JSON shape edits depend on the current code in `inspect.cpp` which I haven't fully read. The implementer must read the existing `do_inspect` body before applying step 5; the patch is shape-only, not exact text.
- Task 8's existing `set-filament` callback may have additional logic beyond the simple example shown; the implementer must preserve it. The key change is the addition of `setfil_part` and the optional-construction in the callback.
- Task 10's XML parsing helper is hand-rolled string scanning; if libslic3r's `model_settings.config` shape ever changes, this helper breaks. Acceptable for a test invariant — it WILL break loudly rather than silently miss.
