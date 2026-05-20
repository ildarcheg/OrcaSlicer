# orca-cli: `object split-to-parts` + per-volume filament — Design

**Date:** 2026-05-20
**Status:** Brainstormed; awaiting plan + implementation
**Scope:** v3 of orca-cli (one new verb + one extended verb + one extended verb output)

---

## Goal

Enable headless multi-material printing workflows by exposing OrcaSlicer's "Split to Parts" operation through the CLI, paired with per-volume filament-slot assignment. After this lands, a user can take a single STL containing N connected mesh components, split it into one ModelObject with N ModelVolumes, and assign a different filament slot to each part — all from a shell script, without opening the GUI.

## Non-goals (out of scope for this spec)

- "Split to objects" (the GUI's other split operation that produces separate `ModelObject`s with independent positions). May be added later.
- Splitting multi-volume objects (modifier meshes, support enforcers, negative volumes). Refuses with `invalid_state`.
- Per-volume config keys beyond `extruder`. Other per-volume keys (`wall_loops`, `sparse_infill_density`, etc.) are addressable via a future extension.
- Line-width config discoverability. Deferred to a follow-up spec — the user wants a way to list config keys filtered by name (e.g., all `*_line_width` keys), which is a separate ergonomic feature.
- Mesh repair, simplification, hollowing, or any other mesh-editing operation. Split is pure connected-component decomposition.

## Architecture summary

One new function and one extended function in `src/cli/project_ops.{hpp,cpp}`:

- `split_object_to_parts(ProjectState&, name)` — delegates to libslic3r's `ModelVolume::split()` (`src/libslic3r/Model.cpp:2742`) with `remap_paint=true`.
- `set_object_filament(ProjectState&, name, slot, optional<part_name>)` — existing function gains an optional `part_name` parameter. Without it: behaviour unchanged. With it: writes to the named volume's per-volume `extruder` config instead of the object-level config.

The CLI dispatch layer in `src/cli/commands/object.cpp` adds a new `split-to-parts` subcommand and a new `--part` flag on `set-filament`. Both route through the existing `MutationExceptionMap` + `run_mutation` envelope (T8 unification). Save goes through the standard `.tmp` + rename + invariant-guard pipeline.

`inspect` (`src/cli/commands/inspect.cpp`) gains a `volumes` array per object in JSON mode and a `volumes:` block in human mode (only emitted when an object has more than one volume).

No changes to `io.cpp`, `invariants.cpp`, `placement.cpp`, or `png_placeholder.cpp` — the split operation is a Model-level mutation and the existing save pipeline handles it correctly.

---

## 1. CLI surface

### New verb: `object split-to-parts`

```
orca-cli object split-to-parts <file> --name X [--output O]
```

Splits the named `ModelObject` into multiple `ModelVolume`s (one per connected mesh component) within the same object. The object keeps its name, position, plate assignment, and instance count. The new volumes are named `X_1`, `X_2`, ..., `X_N` following libslic3r's convention (`Model.cpp:2785`).

### Extended verb: `object set-filament` gains `--part`

```
orca-cli object set-filament <file> --name X [--part Y] --filament N [--output O]
```

- **Without `--part`:** existing behaviour — sets the object-level `extruder` config (`obj->config.set("extruder", N)`).
- **With `--part Y`:** sets the per-volume `extruder` config (`vol->config.set("extruder", N)`) on the volume named `Y` inside object `X`.

`--part` is an optional CLI11 option (empty string when absent — same convention as `--name` on `object add`).

### Extended verb: `inspect` shows per-volume info

The `--json` `data.objects` array gains a `volumes` field per object:

```json
{
  "objects": [
    {
      "name": "multipart",
      "config_keys": ["wall_loops"],
      "volumes": [
        {"name": "multipart_1", "extruder": 1},
        {"name": "multipart_2", "extruder": 2},
        {"name": "multipart_3", "extruder": 3}
      ]
    }
  ]
}
```

Human-mode output gains a `volumes:` indented block under each object that has more than one volume. Objects with a single volume don't print the block — keeps the diagnostic dump terse for the common case.

### What stays unchanged

- Existing `object add`, `object remove`, `object list` — unchanged.
- `set-filament` without `--part` — unchanged behaviour, unchanged exit codes.
- All other CLI surface (plate, config, project init) — unchanged.

---

## 2. Behaviour semantics

### `split_object_to_parts(ProjectState& s, const std::string& name)` in `project_ops.cpp`

1. Find the named `ModelObject` via `find_object_or_throw(s, name)` — throws `std::out_of_range` if missing → exit 6 via `MutationExceptionMap` default.
2. Validate single-volume precondition: `obj.volumes.size() != 1` → throw `std::invalid_argument("cannot split: object already has multiple volumes; use object split-to-objects first")` → exit 7.
3. Validate volume type: `vol.type() != ModelVolumeType::MODEL_PART` → throw `std::invalid_argument("cannot split: only model parts can be split")` → exit 7. Modifier/support volumes don't have mesh components to split into parts.
4. Call `vol.split(filament_count, /*remap_paint=*/true)` where `filament_count = filament_slot_count(*s.project_config)`. libslic3r returns the number of resulting volumes.
5. If return value `== 1` (single connected component, no split occurred), throw `std::invalid_argument("cannot split: object has only 1 connected mesh component")` → exit 7.
6. **Source-attribution preservation (Bug C defense).** After `vol.split()` returns, every resulting `ModelVolume` MUST carry the parent volume's `source.input_file`, `source.object_idx`, and `source.volume_idx`. The original orca-cli design § 8 added "Bug C" for missing `source_file` causing some Orca/Bambu GUI versions to silently drop volumes on load; split must not re-introduce that bug class. The helper first checks whether libslic3r already propagated these fields from the parent volume; if any new volume's `source.input_file` is empty, the helper stamps it (and the index pair) from the parent and asserts the result. Either path leaves all post-split volumes with valid source attribution.
7. Save via the existing pipeline — atomic `.tmp` + rename, invariant guards run on the saved archive.

**Instance preservation.** Split modifies volumes only; the ModelObject's `instances` vector is untouched. An object created with `--count N` before split remains a single ModelObject with N instances after split, with each instance now rendering all M post-split volumes.

**Object-level `extruder` is not cleared.** Split does not touch `obj->config`. The object-level `extruder` value (set by an earlier `set-filament` without `--part`) remains in place. After `set-filament --part Y --filament Z`, the per-volume value on Y shadows the object-level value for Y's mesh, but the object-level value remains for any volume that lacks a per-volume override. `inspect` shows both: `objects[].config_keys` reflects the object-level config; `objects[].volumes[].extruder` reflects each volume's effective extruder (per-volume override if set, else falling back to object-level).

libslic3r's `vol.split()` internally handles:

- Painting preservation (passed `remap_paint=true`)
- Text-configuration reset (text objects lose their text config on split — silent in libslic3r)
- Per-volume `extruder` initialised to the original volume's `extruder_id()` — so each new part starts with the original filament slot until the user assigns differently
- Mesh sanity — faces with `<3` triangles are dropped
- Convex-hull and unique-ID recomputation per new volume

### `set_object_filament(ProjectState&, name, slot, optional<part_name>)` in `project_ops.cpp`

Signature change:

```cpp
// Before
void set_object_filament(ProjectState& s, const std::string& name, int slot);

// After
void set_object_filament(ProjectState& s, const std::string& name, int slot,
                         std::optional<std::string> part_name = std::nullopt);
```

Existing call sites (P5 era) pass no third argument; default `std::nullopt` preserves their behaviour.

1. Find the named `ModelObject` via `find_object_or_throw`.
2. Validate slot range: `1 <= slot <= filament_slot_count(*s.project_config)` → throw `std::out_of_range` if out → exit 6 (existing logic).
3. If `part_name` is supplied:
   - Linear-scan `obj.volumes`; find the volume whose `name` matches. If none → `throw std::out_of_range("part not found: " + *part_name)` → exit 6.
   - `vol->config.set("extruder", slot)` (per-volume config).
4. If `part_name` is `std::nullopt`:
   - Existing behaviour: `obj->config.set("extruder", slot)`.

### Inspect extension

In `inspect.cpp`'s render loop, for each object call a new helper `object_volume_info(s, name)` in `project_ops.{hpp,cpp}` returning `std::vector<VolumeInfo>` where `VolumeInfo = {std::string name; int extruder;}`. The extruder value is read from `vol->config.option<ConfigOptionInt>("extruder")` if set, falling back to `obj->config.option<...>("extruder")` if not, falling back to `1` if neither.

Build the JSON `volumes` array from this; in human mode emit the block only when `volumes.size() > 1`.

### Save-pipeline invariants — verified unchanged

- `verify_relationships`: insensitive to volume changes (only touches PNG references in `.rels`).
- `verify_plate_thumbnails`: insensitive (only validates plate PNGs).
- `verify_vector_config_roundtrip` (T15): config-only reparse via `LoadStrategy::LoadConfig`. Does not load `Model`, so per-volume config is not exercised by this guard. No regression.

No new invariant guard is required for split. The existing pipeline correctly validates that the saved archive is loadable.

---

## 3. Edge cases and exit codes

### Exit-code map

| Condition | Exit code | Constant | Message |
|---|---|---|---|
| Input file missing | 2 | `file_not_found` | `input not found: <path>` |
| `--name` references unknown object | 6 | `unknown_reference` | `object not found: <name>` |
| `--part` references unknown volume on the object | 6 | `unknown_reference` | `part not found: <name>` |
| Object has multiple volumes already | 7 | `invalid_state` | `cannot split: object already has multiple volumes; use object split-to-objects first` |
| Object's volume is not a `MODEL_PART` | 7 | `invalid_state` | `cannot split: only model parts can be split` |
| Mesh has only 1 connected component | 7 | `invalid_state` | `cannot split: object has only 1 connected mesh component` |
| Filament slot out of range | 6 | `unknown_reference` | `filament slot N out of range (1..M)` (existing wording) |
| Any other libslic3r exception | 3 | `parse_failure` | exception message (MutationExceptionMap fallback) |
| Save invariant violation | 8 | `invariant_violation` | invariant message |

`split-to-parts`'s `MutationExceptionMap` configuration:

```cpp
MutationExceptionMap em;
em.set_default_invalid_argument(ExitCode::invalid_state);
// out_of_range default = unknown_reference (unchanged)
```

This mirrors `do_plate_remove`'s override for `invalid_argument` → `invalid_state`. No custom `on<T>()` handlers — every error type is covered by defaults.

### Idempotency

- `split-to-parts` is **not idempotent**. Running twice on the same object: first run succeeds; second run errors with exit 7 (`already has multiple volumes`). This is the desired behaviour — better than silently double-splitting or silently no-opping.
- `set-filament --part` is **idempotent in STATE** (re-running with the same slot leaves the model byte-identical) but **NOT in SIDE-EFFECTS** (every call still triggers a full `.tmp` write + invariant-guard run + rename swap, since the CLI does not diff before saving).

### `--part` on single-volume objects

If the user passes `--part X_1` on an object that still has only its original single volume (and that volume happens to be named `X_1`), the call succeeds and writes to the volume's per-volume config. Per-volume config takes precedence over object-level config in libslic3r's resolution, so the effect is identical to the object-level path for a single-volume object. We don't refuse this case — the user might be writing a script that uses `--part` uniformly.

### `--part` with empty string

CLI11 rejects empty values for required-value options at parse time. `--part` is declared with a default of `""` (CLI11's "unset" sentinel for string options), so passing `--part ""` is indistinguishable from not passing `--part` and falls through to the object-level write. This matches the convention used for `--name` on `object add` (empty defaults to STL basename).

### Painting preservation

`vol.split(..., remap_paint=true)` preserves MMU painting across the split. The current orca-cli test suite doesn't exercise painting (and shouldn't — it's a GUI-level concern), but the behaviour is documented in a comment on the helper.

### Round-trip safety

After split:
- Model is mutated in memory: volume count goes from 1 to N.
- `save_project` writes via the existing path. `store_bbs_3mf` serializes the new volumes into `Metadata/model_settings.config` and the relationships file. Invariant guards verify the archive is well-formed.
- A subsequent `load_project` recovers the same N-volume state. The new `test_split.cpp` roundtrip test covers volume-count and per-volume `extruder` survival.

---

## 4. Tests

All tests follow existing conventions: Catch2 v3 (`<catch2/catch_all.hpp>`), `[orca-cli]` prefix tag, no `&&` inside `REQUIRE`, fixture macros from `tests/cli/test_common.hpp`. New tests share an `[orca-cli][split]` tag for filterability.

### Fixture setup — two-layer model

Matches the canonical orca-cli design § 6.2: a committed CI-portable synthetic fixture plus an optional realistic local-dev fixture.

**Layer A — committed, CI-portable**: `tests/cli/fixtures/two_cubes.stl`.

A ~700-byte binary STL containing two disjoint axis-aligned cubes (e.g., a 10 mm cube at the origin and a 10 mm cube offset by `(30, 0, 0)`), generated at fixture-prep time by a small in-tree helper at `tests/cli/fixtures/gen_minimal_stls.cpp` (following the pattern from the original orca-cli plan). The generator is a CMake-built executable that emits the .stl into the source tree; running it is idempotent and the produced file is committed to git. Tests reference this file via a compile-time define `ORCA_CLI_FIXTURES_DIR` pointing at `tests/cli/fixtures/`. Layer A is the canonical fixture for every unit test, every roundtrip test, and any e2e that needs a deterministic component count. It is always present in CI.

**Layer B — realistic, local-dev only**: `box_with_text.stl` lives in `slicer_tamplates\` (existing `ORCA_CLI_STL_DIR` cache variable). It is NOT committed to git. The file (~35 KB) contains a box with embossed text glyphs as separate meshes, expected to produce 4–10+ connected components on split. Layer B is the fixture for the manual smoke recipe and exactly one e2e test that exercises a more interesting multi-component mesh. The e2e test cleanly `SKIP`s when the file is missing — CI does not fail without it.

`reference_orca_cli_fixtures.md` in memory will be updated to list both layers and their intended use.

### Fixture sanity tests (added first, gate everything else)

```cpp
TEST_CASE("two_cubes.stl is two-component (Layer A fixture sanity)",
          "[orca-cli][split][fixture]") {
    namespace fs = boost::filesystem;
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

TEST_CASE("box_with_text.stl is multi-component (Layer B fixture sanity)",
          "[orca-cli][split][fixture]") {
    namespace fs = boost::filesystem;
    const auto path = fs::path(ORCA_CLI_STL_DIR) / "box_with_text.stl";
    if (!fs::exists(path)) {
        SKIP("box_with_text.stl not present at " << path.string()
             << " — copy it from C:/Users/ildarcheg/Documents/GitHub/");
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

Layer A's sanity fails hard if `two_cubes.stl` is missing or wrong — it is a build artifact and must exist. Layer B's sanity `SKIP`s cleanly when absent; the realistic e2e that depends on it `SKIP`s for the same reason. Either way, no cascading mystery failures.

### Unit tests — `tests/cli/unit/test_project_ops.cpp` (extended)

Tagged `[orca-cli][split]`. Unit tests use **Layer A's `two_cubes.stl`** for deterministic 2-component behaviour.

1. `split_object_to_parts produces N volumes from N-component mesh` — add Layer A STL, call helper, assert `obj.volumes.size() == 2`, assert names follow `X_1`, `X_2` pattern.
2. `split_object_to_parts on single-component mesh throws invalid_argument` — call on a plain cube object (1 component), expect `REQUIRE_THROWS_AS(..., std::invalid_argument)`.
3. `split_object_to_parts on multi-volume object throws invalid_argument` — split once successfully, then call again → expect throw.
4. `split_object_to_parts on unknown object throws out_of_range` — exercise the `find_object_or_throw` path.
5. `split_object_to_parts preserves source attribution on every new volume` — Bug C defense. After split, assert every volume in the resulting object has a non-empty `source.input_file` equal to the STL path used in `add_object`, AND well-defined `source.object_idx` / `source.volume_idx`. Catches a regression where libslic3r changes the propagation behaviour or where the helper's stamping logic regresses.
6. `set_object_filament with part_name targets the volume's config` — after split, set `--part X_1 --filament 2`; assert `vol[0]->config.option("extruder") == 2` AND `vol[1]->config` unchanged.
7. `set_object_filament with unknown part_name throws out_of_range` — exercise the part-not-found path.
8. `set_object_filament without part_name still hits object-level config` — regression pin for the existing P5 behaviour.

### E2E tests — new file `tests/cli/e2e/test_split.cpp`

Tagged `[orca-cli][split][e2e]`. E2E tests use **Layer A's `two_cubes.stl`** by default; one e2e additionally exercises Layer B (`box_with_text.stl`) and `SKIP`s when absent.

Every e2e test that produces a saved 3mf MUST run the following archive-level invariants on the output (helpers already exist in `tests/cli/archive_invariants.{hpp,cpp}` from the original design § 6.1):

- **Invariant #4 (source attribution):** every `<part>` block in `Metadata/model_settings.config` carries a non-empty `source_file` attribute. Bug C class — if this fails, some Orca/Bambu GUI versions silently drop the volume on load.
- **Invariant #5 (per-volume extruder):** after `set-filament --part Y --filament Z` on a split object, the `<part>` block for volume `Y` in `Metadata/model_settings.config` carries `extruder = Z`. Asserted in addition to (not instead of) the `inspect --json` state check.

The two invariants run on every e2e test that exercises per-volume filament assignment, alongside `inspect --json` parsing.

1. End-to-end split + per-part filament assignment (Layer A) — full recipe from manual-test.md Phase 8 using `two_cubes.stl`. Asserts: `inspect --json` shows the expected `volumes` array; invariant #4 on the saved 3mf; invariant #5 on the saved 3mf for each per-volume `set-filament` call.
2. End-to-end split + per-part filament (Layer B, realistic) — same flow with `box_with_text.stl`. `SKIP`s when Layer B fixture absent. Asserts invariant #4 and #5 the same way.
3. Split refuses single-component object — `object split-to-parts` on the plain cube → exit 7, error message contains `"only 1 connected"`.
4. Split refuses multi-volume object (idempotency) — split, then split again → exit 7, error message contains `"multiple volumes"`.
5. Split refuses unknown object — exit 6.
6. `set-filament --part` rejects unknown part name — exit 6.
7. `--output` side-car: split writes only to side-car path; original input untouched.

### Roundtrip tests — new file `tests/cli/roundtrip/test_split.cpp`

Tagged `[orca-cli][split][roundtrip]`. Uses Layer A for determinism.

1. Volume count survives load/save — split into 2 parts (Layer A), save, load fresh, assert volume count and names.
2. Per-part `extruder` survives load/save — split, assign filaments 1 and 2 to the two parts, save, load, assert each volume's `extruder` config equals its assigned slot.

### Expected test-count delta

- Fixture sanity: **+2** (Layer A always, Layer B conditional)
- Unit: **+8** (added source-attribution preservation case)
- E2E: **+7** (added Layer B realistic-mesh case)
- Roundtrip: +2

Total: **124 → 143 cases** (was 140 in the original v1 spec; the +3 delta comes from the new Layer A/B sanity split, the source-attribution unit, and the Layer B e2e).

Archive invariants #4 and #5 are additional assertions WITHIN existing e2e cases, not new test cases.

---

## 5. Documentation

### `docs/cli/manual-test.md` — new Phase 8

A new section after the existing Phase 7 cumulative recipe. Pattern matches every prior phase: setup → happy path → anti-cases → manual GUI verification bullets. Full text in Section 5 of the brainstorm transcript.

The existing cumulative P7 recipe gets ONE new line appended near the bottom to exercise split-to-parts as part of the full-surface-area smoke run:

```powershell
& $CLI object add $OUT --plate "Brackets" --stl "$STLS\box_with_text.stl" --name multi
& $CLI object split-to-parts $OUT --name multi
& $CLI object set-filament $OUT --name multi --part multi_1 --filament 1
```

### `docs/cli/status.md` — new Phase 8 section

Mirrors the structure of Phase 7's entry. Bullet list of completed acceptance items (`[x]`) plus the manual GUI smoke bullet (`[ ]` until verified).

### `README.md` — no change

The existing "orca-cli (experimental command-line composer)" pointer still applies.

### Memory updates after implementation

- `project_orca_cli_v2_cleanup.md` gets a "Phase 8 — split-to-parts" follow-up section.
- `reference_orca_cli_fixtures.md` gets `box_with_text.stl` added with its purpose.

---

## Acceptance criteria

The feature is "done" when:

1. `orca-cli object split-to-parts <file> --name X [--output O]` is invocable and behaves per Section 2.
2. `orca-cli object set-filament <file> --name X --part Y --filament N [--output O]` is invocable and behaves per Section 2.
3. `orca-cli inspect <file>` shows the `volumes` array (JSON) / block (human) per Section 1.
4. All 19 new tests in Section 4 pass on Windows Release builds (143 total).
5. Phase 7 cumulative recipe passes manually in OrcaSlicer (visual GUI smoke) including the appended split-to-parts step.
6. No regression in the 124-case baseline test suite.
7. Documentation in `docs/cli/{manual-test,status}.md` updated per Section 5.

## Out of scope (explicit)

- **Line-width discoverability** — listed as a follow-up brainstorm. The user wants `orca-cli config keys [--filter PATTERN]` or similar to enumerate `print_config_def` keys with optional substring matching. This spec does NOT design that feature.
- **Split-to-objects** — the GUI's other split operation. May be added later as a separate verb `object split-to-objects`.
- **Multi-volume splitting** — splitting each volume of an already-multi-volume object. Refused with exit 7.
- **Per-volume keys beyond extruder** — out of scope for this spec. Future extension if needed.

## Spec self-review notes

- **Placeholder scan:** zero TBDs / TODOs.
- **Consistency:** Section 1's exit-code claims are consistent with Section 3's table. Section 2's behaviour matches Section 4's test expectations. The Layer A/B fixture distinction in Section 4 is consistent with the Phase 8 manual-test recipe in Section 5 (manual recipe uses Layer B; e2e tests primarily use Layer A).
- **Scope:** focused on one feature with one closely-related extension (`--part` on `set-filament`). Line-width discoverability explicitly deferred.
- **Ambiguity:** the `--part` on single-volume objects case is explicitly addressed (allowed; same effect as object-level set). The empty-`--part` case is explicitly addressed (treated as absent). The single-component split case is explicitly addressed (refuse with exit 7, not no-op). The object-level vs per-volume `extruder` interaction is explicitly addressed in Section 2 — object-level value is not cleared, per-volume value shadows it for the named volume only.
- **Bug C class defenses:** source attribution preservation is required in Section 2 (helper must verify or stamp), unit-tested in Section 4 test #5, and asserted at archive level on every per-volume e2e via invariant #4. Three independent layers prevent regression.
