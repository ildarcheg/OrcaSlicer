# orca-cli — design spec

**Status:** Approved design. Input to writing-plans.
**Date:** 2026-05-19
**Repo:** `C:\Users\ildarcheg\Documents\GitHub\OrcaSlicer`
**Predecessor:** `C:\Users\ildarcheg\Documents\GitHub\2026-05-19-orca-cli-retrospective.md`
**Backup of prior implementation:** branch `backup/2026-05-19-orca-cli-pre-rollback`, tag `pre-rollback-2026-05-19` (read-only reference; no code copied).

---

## 1. Goal

A command-line tool `orca-cli` (`orca-cli.exe`) that takes bare STL files plus a known-good reference `.3mf` and produces a new `.3mf` the OrcaSlicer GUI opens cleanly — every plate, object, filament assignment, and per-object config override surviving the round-trip.

`orca-cli` is a **composer**, not a slicer. Producing G-code is explicitly out of scope. The product ends at "the `.3mf` opens cleanly in OrcaSlicer with all the right settings already in place." Slicing happens in the GUI.

The user-facing recipe v1 must enable:

```
orca-cli project init out.3mf --template ref.3mf
orca-cli plate add out.3mf --name Brackets
orca-cli object add out.3mf --plate Brackets --stl part.stl --filament 1
orca-cli object add out.3mf --plate Brackets --stl part2.stl --translate 60,60
orca-cli config set out.3mf --object part --key wall_loops --value 4
```

…and `out.3mf` opens in OrcaSlicer with two plates, the placed objects on the new plate, the per-object filament slot assigned, and the per-object override visible in the GUI's object-settings pane.

## 2. Strategy

**Clone-and-mutate only.** There is no synthesize-from-scratch path. `project init` requires `--template <ref.3mf>`. The template's printer / process / filament palette is **authoritative** — v1 does *not* expose `--printer` / `--process` / `--filament` overrides. The user prepares a known-good reference 3mf in the GUI once; the CLI clones and mutates from there.

**Rationale:** v1.1 of the prior implementation succeeded with clone-and-mutate for single-filament recipes; v1.2 broke when it added preset overrides on `project init` (Bug A — `apply_preset_kvs` separator mismatch). v1's spec accepted by the user is "Restart with stricter scope" per the retrospective. Removing the preset-override surface removes the entire bug class.

**Thin orchestrator over libslic3r.** Never hand-write 3mf XML. Every write goes through `Slic3r::store_bbs_3mf`. Every read goes through `Slic3r::load_bbs_3mf` with `LoadStrategy::LoadModel | LoadStrategy::LoadConfig` (G1).

**Save-side invariant guard.** After every `store_bbs_3mf`, the CLI re-opens the produced .3mf as a zip and runs three checks. Failures abort the save (output deleted, exit 8). This is the primary defense against the v1.2 failure mode — Bug B (dangling small-thumbnail relationship) passed all 60 tests but crashed the GUI because nothing verified zip-integrity vs declared relationships.

The three checks:
1. Every `<Relationship>` Target declared in `_rels/.rels` and any `*.rels` file resolves to an entry that exists in the archive.
2. Every plate `N` has both `Metadata/plate_N.png` and `Metadata/plate_N_small.png`.
3. Each vector-typed config value (`coPoint*`, `coPoints*`, vector-bool, vector-string-group) re-deserializes from the saved string to a value equal to the in-memory value (catches Bug A's class).

## 3. Architecture

### 3.1 Repository layout

```
src/cli/
  CMakeLists.txt              new target `orca-cli`; links libslic3r, Boost, OpenSSL
  main.cpp                    dispatcher: parses top-level, routes to commands/*
  globals.{hpp,cpp}           GlobalOpts: --json, --verbose
  output.{hpp,cpp}            print_ok, print_err, ExitCode enum, JSON mode formatter
  io.{hpp,cpp}                load_project, save_project (save_project runs invariants)
  invariants.{hpp,cpp}        verify_relationships, verify_plate_thumbnails,
                              verify_vector_config_roundtrip
  project_ops.{hpp,cpp}       pure mutations: add_plate, remove_plate, add_object,
                              place_object, set_object_filament, set_object_config
  placement.{hpp,cpp}         deterministic grid fallback for headless arrange (G5);
                              per-plate sqrt(n) grid, bed-min anchored
  cli11/CLI11.hpp             vendored header-only CLI11
  commands/
    project_init.{hpp,cpp}    `project init <out> --template <ref>`
    plate.{hpp,cpp}           `plate add|remove|list`
    object.{hpp,cpp}          `object add|set-filament|list|remove`
    config.{hpp,cpp}          `config set|unset|list [--object N]`
    inspect.{hpp,cpp}         `inspect <file>` — debug aid

tests/cli/
  CMakeLists.txt              target `cli_tests` (Catch2)
  fixtures/                   small committed STLs + in-tree generator
  unit/                       project_ops, placement, invariants
  roundtrip/                  in-memory state -> save -> load -> assert equality
  e2e/                        spawns orca-cli.exe via boost::process

docs/cli/
  manual-test.md              PowerShell smoke recipe (cumulative per subagent)
  status.md                   running status doc; updated by each subagent
```

### 3.2 In-memory model

```cpp
struct ProjectState {
    std::unique_ptr<Slic3r::Model>              model;
    std::unique_ptr<Slic3r::DynamicPrintConfig> project_config;
    std::vector<std::unique_ptr<Slic3r::PlateData>> plates;
    Slic3r::PlateDataPtrs plate_data_ptrs() const;
};
```

No `was_loaded_from_disk` flag — every project came from a template by construction. No synthesize-from-scratch path means no need for the conditional save-time patches that v1 needed (`filament_colour` inject, `filament_extruder_variant` trim).

### 3.3 Pipeline

Every command follows the same five-stage pipeline:

```
arg parse + validation (CLI11)
  -> load_project (LoadModel | LoadConfig, rebuild objects_and_instances)
  -> pure mutation in project_ops
  -> save_project: write to out.3mf.tmp; store_bbs_3mf; rename on success
  -> invariant guard: relationships, thumbnails, vector roundtrip
        pass -> print_ok / exit 0
        fail -> delete out.3mf.tmp; exit 8 invariant_violation
```

The split exists so most logic is pure and testable without touching disk.

### 3.4 Out of scope, by design

The CLI will refuse / not implement:
- `slice` subcommand or any G-code generation.
- `project new` or any synthesize-from-scratch path.
- `--printer` / `--process` / `--filament` overrides on `project init`.
- SHARED-SPEC cross-project alignment with `bambu-cli` (deferred).
- `--force` or any escape hatch that downgrades an invariant violation to a warning.
- Logging to files; telemetry.

## 4. Commands (v1 surface)

This section describes the **final v1 surface** — every flag and every command after S7 ships. The subagent sequence in § 7.1 layers these in incrementally (e.g. `--filament` on `object add` arrives in S5, not S3).



### 4.1 `project init <out> --template <ref>`

Atomic copy of `<ref>` to `<out>` (`out.3mf.tmp` + rename). Then runs the pipeline (load → no mutation → save → verify). Verifying after a clone is what catches a *bad* template: if the user's reference fails the invariant guard, they learn it now rather than after their first feature command.

### 4.2 `plate add|remove|list <file>`

| Sub | Mutation |
|---|---|
| `add --name X` | Append `PlateData`, generate placeholder `plate_N.png` and `plate_N_small.png` (128×128 transparent PNG) so G3 holds. Duplicate name → exit 5. |
| `remove --name X` | Remove by name; re-index trailing plates; drop their PNGs. Removing the only plate → exit 7. |
| `list` | Print plates and object counts. No save. |

### 4.3 `object add|set-filament|list|remove <file>`

| Sub | Mutation |
|---|---|
| `add --plate P --stl S [--filament N] [--translate x,y] [--rotate ax,ay,az] [--scale s\|sx,sy,sz] [--name M]` | Load STL via `Model::read_from_file`. Append `ModelObject`. Set `extruder = N` on `ModelObject::config` if `--filament` (G4 — only on per-object writes, not on synthesized projects, which we don't have). Apply transforms if any; otherwise place via deterministic per-plate grid (bed-min anchored, 10 mm margin). Append `(obj_idx, instance_idx)` to target plate's `objects_and_instances`. After transforms, the object's world-space AABB must fall within the target plate's printable area; otherwise exit 9. |
| `set-filament --name M --filament N` | Validate N ≤ filament-slot count from `filament_settings_id` length. Set `extruder = N`. |
| `list [--plate P]` | Print, no save. |
| `remove --name M` | Reverse of add. Rebuild plate→object map. |

`--stl` not `--file` to dodge G9 (CLI11 + MSVC /GS abort).

### 4.4 `config set|unset|list <file> [--object N]`

| Sub | Mutation |
|---|---|
| `set --key K --value V [--object N]` | Validate K against `print_config_def`. With `--object`: write to `ModelObject::config`. Without: write to `project_config`. Type-aware joiner on vector values (Bug A). |
| `unset --key K [--object N]` | Erase the option from the right target. |
| `list [--object N] [--changed-only]` | Read-only; no save. `--changed-only` uses `DynamicPrintConfig::diff` against `new_from_defaults_keys` (G6 — avoid `default_value->serialize()` which crashes on `coEnums`). |

### 4.5 `inspect <file>`

Diagnostic dump only. Plates, objects per plate, filament slots, changed config keys per object and at project level. No save. Useful between feature subagents to confirm round-trip state.

## 5. Error handling

### 5.1 Exit codes

| Code | Name | Trigger |
|---|---|---|
| 0 | `ok` | Success. |
| 1 | `usage_error` | Bad flags, missing required arg, conflicting flags. |
| 2 | `file_not_found` | Input path missing (template, STL, output dir). |
| 3 | `parse_failure` | 3mf load failed, STL load failed, malformed input. |
| 4 | `bad_config` | `config set` key not in `print_config_def`, or value rejected. |
| 5 | `duplicate_name` | Name collision on plate / object add. |
| 6 | `unknown_reference` | `--plate X` / `--object Y` / `--filament N` not found / out of range. |
| 7 | `invalid_state` | Valid operation, invalid for current project (e.g. remove only plate). |
| 8 | `invariant_violation` | Post-save guard tripped. Output deleted. |
| 9 | `placement_failure` | Manual transform off-bed; auto-arrange and grid fallback both failed. |

### 5.2 Output modes

Human (default):
```
ok: added plate 'Brackets' (now 4 plates)
err: invariant_violation: relationship target 'Metadata/plate_2_small.png' missing
```

JSON (`--json`):
```json
{"status":"ok",  "code":"ok",                  "message":"added plate 'Brackets'", "data":{"plate_count":4}}
{"status":"err", "code":"invariant_violation", "message":"relationship target 'Metadata/plate_2_small.png' missing"}
```

JSON shape is stable across all commands: `status`, `code`, `message`, optional `data`.

### 5.3 Atomicity

Every mutating command writes to `<out>.tmp` and renames on success. If the invariant guard trips, the temp file is deleted and the original `<out>` (if any) is untouched. There is no half-written output on disk.

### 5.4 No escape hatches

No `--force`. No `--allow-broken`. If an invariant fails, the user fixes the input. v1.2's Bug B slipped past 60 tests precisely because nothing failed loudly enough.

## 6. Testing

### 6.1 Layers

**Unit** (`tests/cli/unit/`) — pure functions, no disk, fast (<1 s total).
- `project_ops`: add/remove plates and objects on hand-built `ProjectState`.
- `placement`: deterministic grid math.
- `invariants`: feed hand-crafted malformed `ProjectState` / synthetic zips, assert codes trip correctly.

**Round-trip** (`tests/cli/roundtrip/`) — in-process, no subprocess.
- Build `ProjectState`, `save_project` to `$TEMP`, `load_project` back, assert structural equality (plate count/names, object count per plate, filament-slot count, changed config keys).
- Each composer operation has at least one round-trip test.
- Catches G2 by construction.

**E2E** (`tests/cli/e2e/`) — spawns `orca-cli.exe` via `boost::process`.
- Asserts JSON output shape, exit codes for failure modes, invariant guard re-runs cleanly on the produced .3mf.
- One e2e per user-visible recipe.
- ASCII `->` in test names (G8).

### 6.2 Fixtures

Canonical fixtures live in `C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\` (note the literal directory name; sic on spelling). This directory is shared with the sister `bambu-cli` workstream, which uses its own template alongside ours.

| Path | Role |
|---|---|
| `C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\temp_project_for_orca_slicer.3mf` | Reference 3mf for orca-cli. Authored in OrcaSlicer's GUI. **Never modify in place** — every test copies to `$TEMP` first. |
| `C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\000_01_test_cylinder.stl` | Object-add fixture (cylinder). |
| `C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\000_01_test_cone.stl` | Object-add fixture (cone). |
| `C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\000_01_test_bambu_cube.stl` | Object-add fixture (cube, larger). |
| `C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\000_01_test_cube.stl` | Object-add fixture (cube, minimal). |
| `C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\temp_project_for_bambu_studio.3mf` | Sister-project template; **not used by orca-cli**, listed here so we don't accidentally point at it. |

CMake cache vars expose these paths to the test build:

- `ORCA_CLI_REF_3MF` — defaults to `temp_project_for_orca_slicer.3mf` above.
- `ORCA_CLI_STL_DIR` — defaults to the parent directory; individual STL filenames are resolved relative to it.

If any path is unset or missing on the host, the test that needs it `SUCCEED+skip`s so CI without local fixtures still passes. Committed in-tree minimal STLs (small, non-zero normals per G7) live under `tests/cli/fixtures/` for the CI path; the user-tree paths above are the local-development fixtures.

The reference 3mf is **never modified in place**. Every test copies it to `$TEMP` before opening for write. The current canonical fixture set is also pinned in the `[[reference-orca-cli-fixtures]]` memory.

### 6.3 Per-feature manual GUI smoke

Between every two consecutive feature subagents, the user runs a smoke recipe and confirms the produced `.3mf` opens in OrcaSlicer (and optionally Bambu Studio) with no crash and the expected state.

- Each feature subagent appends a section to `docs/cli/manual-test.md` with exact PowerShell commands.
- The recipe is cumulative — feature S5 must still pass S1–S4's recipe sections, so regressions surface at the boundary they introduce.
- Pass → next subagent fires. Fail → main agent diagnoses with the user; the failing subagent may be re-spawned with the diagnosis as input, or the design itself revised.

### 6.4 Build commands

```powershell
$env:Path = "C:\Program Files\CMake\bin;C:\Strawberry\perl\bin;C:\Strawberry\c\bin;$env:Path"
cmake --build build --target orca-cli  --config Release --parallel 2
cmake --build build --target cli_tests --config Release --parallel 2
.\build\tests\cli\Release\cli_tests.exe --order rand --warn NoAssertions
```

`--parallel 2` is deliberate; higher OOMs MSVC PCH on this machine.

## 7. Execution model

The implementation runs as a sequence of subagents (Agent tool, `general-purpose` subtype), one per feature. Main agent (the brainstorming/orchestrator session) does not write feature code — it spawns subagents, waits for completion, prompts the user for manual GUI smoke, and decides what fires next.

### 7.1 Subagent sequence

| # | Name | Scope |
|---|---|---|
| S0 | `scaffold` | New CMake target `orca-cli`; vendored CLI11; `main.cpp` with `--help` and `--version`; `globals.cpp`; `output.cpp` with `ExitCode` + JSON formatter; empty `commands/`; CMake target `cli_tests`; Catch2 wired; one trivial e2e test (`orca-cli --version` returns 0). |
| S1 | `project-init` | `commands/project_init.cpp`; `io.cpp` (load_project, save_project skeleton); `invariants.cpp` (all 3 guards); round-trip + e2e tests; manual smoke section #1 (clone-and-mutate, no mutation). |
| S2 | `plate-ops` | `commands/plate.cpp` (add/remove/list); placeholder PNG generator; `project_ops` plate functions; tests; manual smoke section #2. |
| S3 | `object-add` | `commands/object.cpp` (add/list/remove, auto-place only); `placement.cpp` grid fallback; STL loader; tests; manual smoke section #3. |
| S4 | `object-transforms` | `--translate`/`--rotate`/`--scale` on `object add`; off-bed check; tests; manual smoke section #4. |
| S5 | `object-filaments` | `object add --filament N`; `object set-filament`; slot validation; tests; manual smoke section #5. |
| S6 | `config-set` | `commands/config.cpp` (set/unset/list, project + per-object); type-aware joiner; tests; manual smoke section #6. |
| S7 | `docs-finalize` | `inspect` command; `docs/cli/status.md` finalize; manual-test.md cumulative recipe; README pointer. |

S0–S7 commit locally per subagent. Nothing pushed.

### 7.2 Subagent contract

Each subagent receives a prompt containing:
- This spec document.
- The implementation plan (output of `writing-plans`, next step).
- The specific feature scope (one section of the plan).
- Read access to `backup/2026-05-19-orca-cli-pre-rollback` for reference (lessons, not copy).
- The 9 gotchas G1–G9 inline.
- Build + test commands.

Each subagent must:
1. Write code in `src/cli/` (no edits outside `src/cli/`, `tests/cli/`, `docs/cli/`, `CMakeLists.txt`).
2. Add Catch2 tests in all three layers as appropriate.
3. Run `cli_tests.exe --order rand --warn NoAssertions` and confirm green before committing.
4. Append a section to `docs/cli/manual-test.md`.
5. Commit locally with a `feat(cli):` / `test(cli):` / `docs(cli):` prefix.
6. Report back: list of commits, test counts, smoke recipe summary.

### 7.3 Manual GUI smoke gate

After every subagent reports done:
1. Main agent surfaces the smoke recipe section to the user.
2. User runs the recipe and opens the produced `.3mf` in OrcaSlicer.
3. User reports `pass` or `fail`.
4. `pass` → main agent spawns the next subagent.
5. `fail` → main agent diagnoses with the user; possible outcomes: spec revision, subagent respawn with diagnosis as input, or rollback of the subagent's commits.

The smoke recipe is the only place the design's correctness is finally verified. Tests are necessary but not sufficient — Bug B in v1.2 proved that.

## 8. Lessons baked in (G1–G9 + Bug A + Bug B)

The retrospective enumerates 9 shared gotchas plus two latent save-path bugs. Every one is encoded in this design rather than carried as tribal knowledge:

| # | Lesson | Where it lives in this design |
|---|---|---|
| G1 | `LoadStrategy::Default = 0` skips model AND config | `io.cpp::load_project` always passes `LoadModel \| LoadConfig` |
| G2 | `objects_and_instances` not populated on load | `io.cpp::load_project` rebuilds from `obj_inst_map` ↔ `loaded_id` |
| G3 | Thumbnail relationships unconditional | Invariant guard check #1 + placeholder PNG generator in plate add |
| G4 | `filament_colour` / `extruder_variant` patches damage loaded projects | Not needed — no synthesize path means no patches |
| G5 | `arrangement::arrange` returns UNARRANGED headlessly | `placement.cpp` deterministic grid fallback |
| G6 | `coEnums` `default_value->serialize()` crashes | `config list --changed-only` uses `DynamicPrintConfig::diff` |
| G7 | STL fixtures need non-zero normals | In-tree fixture generator enforces this |
| G8 | ctest mangles non-ASCII test names on Windows | ASCII `->` convention |
| G9 | `--file` collides with CLI11 + MSVC /GS | `--stl` everywhere |
| Bug A | `apply_preset_kvs` separator mismatch | Not reachable — no preset overrides in v1 surface |
| Bug B | Dangling small-thumbnail relationship | Invariant guard check #1 + placeholder PNG for new plates |

## 9. Open questions deferred to v2+

- Multi-filament `project init` overrides (`--filament` repeatable). Requires the audited save path that v1 explicitly defers.
- Switching `--printer` on `project init`. Same blocker.
- Multi-volume `ModelVolume` filament/config assignment. v1 only handles `ModelObject`-level.
- `bambu-cli` cross-project SHARED-SPEC alignment.
- Linux / macOS support. v1 is Windows-only by virtue of build commands; the code is portable but untested elsewhere.

---

*End of design spec. Next step: invoke `superpowers:writing-plans` to produce the implementation plan that subagents will execute.*
