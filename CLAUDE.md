@AGENTS.md

# orca-cli — session context

## What this project is
A standalone GUI-free CLI (`orca-cli`) that composes BBS `.3mf` projects
by driving libslic3r's load/store directly. Lives under `src/cli/` +
`tests/cli/`. Parallel to the sibling `bambu-cli` initiative in the
BambuStudio fork at `C:\Users\ildarcheg\Documents\GitHub\BambuStudio`
(read-only reference; feature-parity is the explicit goal).

## Architecture in one paragraph
`orca_cli_core` static lib holds all logic; both the `orca-cli` exe and
the `cli_tests` Catch2 binary link it (no drift). All mutations go
through libslic3r's own `load_bbs_3mf` / `store_bbs_3mf` / `Model` /
`DynamicPrintConfig` / `PlateData` / `print_config_def` — no upstream
monkey-patching, no `#ifdefs`, only an `add_subdirectory(cli)` hook in
`src/CMakeLists.txt` and `tests/CMakeLists.txt`. Save path is the
`.bak`-swap atomic pattern in `io.cpp` with a three-check post-write
invariant guard (rels target resolution / per-plate thumbnails /
vector-config roundtrip).

## Sibling-fork divergences — LEGITIMATE, do not try to "fix"
- Profile storage: Orca mirrors `model.profile_info` into
  `metadata_items["ProfileTile"]` (upstream typo, intentional);
  Bambu reads `model.profile_info` directly.
- Aux folder names: Orca uses `pictures`/`bom`/`assembly-guide`/`others`
  (lowercase, hyphenated); Bambu uses TitleCase. Reflects upstream
  `Auxiliaries/` directory naming.
- CLI link surface: Orca's `orca_cli_core` links `libslic3r` only (not
  `libslic3r_gui` where `Http.cpp` lives), so no stubs needed. Bambu has
  `LogSink` references inside `libslic3r` itself plus `Http` from
  `libslic3r_gui`, and uses `stubs_for_libslic3r.cpp` to no-op them. Do
  NOT propose adding stubs to Orca; structurally unnecessary.

## Misattribution to watch for
`.bak`-swap atomic save originated here (Orca M11, `src/cli/io.cpp:467-499`).
Bambu ported it FROM Orca. The Bambu-side code comment is correct.

## Branch state (as of 2026-05-23)
- `main` HEAD: `67c5848` ("fix(cli): open input template via
  mz_zip_reader_init_file (short-path tolerance)") — the cross-project
  convergence work.
- `origin/main` matches HEAD exactly: convergence IS pushed, working
  tree is clean. (The earlier "1 commit ahead, not pushed" status in
  prior notes is stale.)
- `cross-project-convergence` branch retained, points at the same SHA.
- Last `cli_tests` run: 66,871 assertions, 0 failures (during Item 4
  testing on 2026-05-22).

## File layout
- `src/cli/` — entry (`main.cpp`), `io.cpp`, `project_ops.{hpp,cpp}`,
  `project_tab_ops.{hpp,cpp}`, `placement.{hpp,cpp}`, `invariants.{hpp,cpp}`,
  `png_placeholder.{hpp,cpp}`, `output.{hpp,cpp}`, `globals.{hpp,cpp}`,
  `commands/` (one TU per top-level verb), `cli11/CLI11.hpp` vendored.
- `tests/cli/` — `unit/`, `roundtrip/`, `e2e/` subtrees; fixtures in
  `tests/cli/fixtures/`.
- `docs/cli/` — `build.md`, `manual-test.md`, `status.md`, `notes/`.
- `docs/superpowers/specs/` and `plans/` — design docs + phase plans.

## Open items (carryover, not regressions)
- `docs/cli/status.md` manual GUI smoke gates still `[ ]` for several
  pre-convergence milestones (P1–P6 in particular). The cumulative P7
  recipe and the P8/P9/P10 recipes were exercised live; the per-phase
  checkboxes are pre-existing and not addressed in the convergence pass.
- `--object` group-by-name semantics retrofit (commits `c2ddf51d87`,
  `fa9ec76e2c`, `b32bccdfb3`, `f7a2399f73`) landed for `remove`,
  `set-filament`, `config set/unset/list` — but not every object
  subcommand has been audited end-to-end against the new semantics.
- Strict per-volume config-agreement rule in `merge-parts` is
  deliberately strict per spec § 3.5 ("easier to relax later than to
  retrofit"). If Bambu has a looser rule, that's a known divergence.
- `source_path` lifetime contract on `ProjectState` (`project_ops.hpp:62`):
  deleting the source `.3mf` between load and save makes the thumbnail
  passthrough produce an archive the invariant guard then rejects
  (loud-failure, not silent corruption).
- CLI11 + MSVC option-name collision papercut: object subverbs use
  `--stl` (not `--file`) to avoid an MSVC `/GS` abort
  (`src/cli/commands/object.cpp:265-268`).

## Detailed references
- `docs/cli/notes/2026-05-22-orca-cli-audit.md` — full Round-0 audit
  (every file, every feature, every integration point with file:line refs).
- `docs/cli/notes/2026-05-22-cross-project-convergence-log.md` — what
  each convergence item did and why (item 4 landed; items 1–3 verified
  already-converged or direction-reversed).
- `docs/cli/status.md` — phase-by-phase implementation tracker.
- `docs/cli/manual-test.md` — cumulative manual smoke recipes per phase.
- `docs/cli/build.md` — CMake + Catch2/MSVC build notes.
