# OrcaSlicer fork — change report

Cross-project comparison report for the OrcaSlicer side of the `orca-cli` /
`bambu-cli` fork pair. Snapshot taken on the `main` branch.

---

## Baseline

- **upstream remote**: `upstream  https://github.com/OrcaSlicer/OrcaSlicer.git`
- **baseline commit**: `46e47cec0a62f3ddf357ab8b8b8bfdab554ca0b3`
  ("Fix ObjectTable crash on cell deselect after wxWidgets 3.3.2 upgrade (#13740)",
  2026-05-19)
- **how identified**: `git merge-base HEAD upstream/main` — clean merge-base
  exists because `upstream` is configured and HEAD descends from it.
- **HEAD**: `a7748fbd2032e2e975b5dafba8456dbb34148243`
- **divergence**: 174 commits, 98 files touched, **+37,893 / -64** lines.
  Of those 174 commits, **169 are local CLI work by `ildarcheg`** and
  **5 are upstream PRs** that landed on `origin/main` after the baseline
  (PR #13575, #13720, #13738, #13741, #13747 — see "Integration Points"
  for the exact files and SHAs).

---

## Summary of Added Functionality

All of the local additions form a single coherent initiative: **`orca-cli`**, a
new companion binary that composes `.3mf` projects without driving the
OrcaSlicer GUI. Each bullet is one distinct user-visible capability.

- **`orca-cli` binary** — new command-line composer (`src/cli/main.cpp`) using
  the vendored CLI11 v2.4.2 (`src/cli/cli11/CLI11.hpp`), with `--json`,
  `--verbose`, `--version`, and a 10-value `ExitCode` taxonomy.
- **Project clone-and-mutate** — `orca-cli project init <out> --template <ref>`
  takes a known-good reference `.3mf`, validates it, and re-saves a fresh copy
  through the load/save pipeline (avoids synthesizing 3mfs from scratch, which
  the v1 attempt got wrong).
- **Plate CRUD** — `orca-cli plate {add,remove,rename,list}`. Plate renumbering
  drops orphan PNGs; rename preserves `plate_index`; placeholder PNGs are
  injected for new plates with no source thumbnail.
- **Object CRUD + transforms + filament slot** — `orca-cli object
  {add,remove,list,set-filament}`. `add` supports `--count N`, `--translate`,
  `--rotate`, `--scale`, `--filament`, and a deterministic per-plate grid for
  stack-free placement.
- **Object split-to-parts** — `orca-cli object split-to-parts --name N`
  decomposes a single mesh into one `ModelVolume` per connected component,
  preserving plate assignment, instance count, source attribution.
- **Object merge-parts** — `orca-cli object merge-parts --name N --parts a,b,...
  --into M [--filament S]` is the inverse: triangle-concatenate selected
  volumes back into one, with strict per-volume config-agreement rules and
  lowest-existing-index placement for stability.
- **Per-volume filament targeting** — `object set-filament --part <vol>` aims at
  a specific post-split volume rather than the whole object.
- **Config set/unset/list** — `orca-cli config {set,unset,list}` reads/writes
  project-wide AND per-object (`--object`) keys via libslic3r's own
  `print_config_def` + `set_deserialize` (same code path the GUI uses).
  `list --changed-only` diffs against libslic3r defaults.
- **Inspect (read-only)** — `orca-cli inspect <file>` dumps Model + plates +
  project config + per-volume info as JSON or plaintext.
- **Project info editing** — `orca-cli project info {show,set,clear}` edits
  `ModelInfo` title / description / license / copyright / cover (cover is
  PNG-only and routes through an embedded `Auxiliaries/.thumbnails/` blob).
- **Project profile editing** — `orca-cli project profile {show,set,clear}`
  does the same for `ModelProfileInfo` (title, description, cover; user_id
  and user_name remain read-only).
- **Auxiliary file management** — `orca-cli project aux {list,add,remove,export}`
  operates on the four GUI-visible buckets (`pictures`, `bom`,
  `assembly-guide`, `others`) with Windows-reserved-name rejection,
  collision-guard + `--force`, and dir-or-file export targets.
- **Runtime archive invariant guards** — every save runs three checks before
  the atomic `.tmp → target` rename: dangling-`.rels`-target, missing
  `plate_N_small.png` sibling, vector-config save/load roundtrip. Failure
  surfaces as `ExitCode::invariant_violation` (exit 8) and the file at the
  destination is left untouched.
- **Cover-image refcount** — `info` and `profile` share the same on-disk
  thumbnail; clearing one nulls only its pointer; the file is deleted only
  when both pointers are empty.
- **Source attribution stamping ("Bug C" defense)** — every new or split
  `ModelVolume` is stamped with `source.input_file` so OrcaSlicer/BambuStudio
  GUIs don't silently drop the part on load.
- **Catch2 test harness** — 13 unit / roundtrip / e2e test files (~6,300 LOC)
  exercising the CLI both as a child process and as in-process library calls
  against `orca_cli_core`.
- **Specs + plans + status docs** — 4 design specs, 5 implementation plans,
  3 user-facing CLI docs (`build.md`, `manual-test.md`, `status.md`).

---

## Changed Files

### Upstream files modified (5 files, 199 lines net) — see "Integration Points" for the PR mapping

- `README.md` — added the orca-cli paragraph pointing at `docs/cli/`.
- `localization/i18n/it/OrcaSlicer_it.po` — Italian "scarf seam" wording
  refinements (upstream PR #13720).
- `src/CMakeLists.txt` — single `add_subdirectory(cli)` hook.
- `src/libslic3r/Print.cpp` — skip jerk warnings when junction-deviation is in
  use (`Print.cpp:1754` block; upstream PR #13575).
- `src/libslic3r/GCode/GCodeProcessor.cpp` — track `current_layer_id` during
  post-process so first-layer toolhead preheat uses the first-layer temp
  (`GCodeProcessor.cpp:1233`, upstream PR #13741).
- `src/slic3r/GUI/GUI_App.cpp` — Wayland preview crash defer-and-retry
  (`GUI_App.cpp:787`) and Orca-Cloud HTTP error notifications
  (`GUI_App.cpp:4849+`); upstream PRs #13747 + #13738.
- `src/slic3r/GUI/GUI_App.hpp` — rename `m_show_http_errpr_msgdlg` →
  `m_show_http_error_msgdlg` (typo fix from PR #13738).
- `src/slic3r/GUI/ImGuiWrapper.hpp` — promote `display_initialized()` to
  public so the HTTP-error handler can gate its notification path (PR #13738).
- `tests/CMakeLists.txt` — single `add_subdirectory(cli)` hook.

### orca-cli source (35 new files, ~5,400 LOC of C++ + a 10,998-line CLI11.hpp)

CLI entrypoint and infrastructure (`src/cli/`):
- `main.cpp` — registers global flags and 5 top-level subcommands.
- `CMakeLists.txt` — builds `orca_cli_core` static lib + `orca-cli` exe,
  mirrors OrcaSlicer DLLs next to the binary on Windows.
- `globals.{hpp,cpp}` — `GlobalOpts` struct (`--json`, `--verbose`, `--output`).
- `output.{hpp,cpp}` — `ExitCode`, `print_ok` / `print_err`,
  `emit_list_response<>` template, JSON envelopes via `nlohmann::json`.
- `io.{hpp,cpp}` — `load_project`, `save_project` (atomic `.tmp → rename`),
  thumbnail passthrough, orphan-PNG pruning, `resolve_save_target`,
  `check_input_exists`.
- `invariants.{hpp,cpp}` — 3 runtime archive guards
  (`verify_relationships`, `verify_plate_thumbnails`,
  `verify_vector_config_roundtrip`) + a standalone input-template guard;
  helpers `unzip_to_memory`, `enumerate_zip_entry_names`,
  `extract_entry_to_memory`.
- `project_ops.{hpp,cpp}` — pure in-memory mutations: plate ops, object
  ops, transforms, filament-slot stamping, config set/unset, split, merge.
  Defines typed exceptions: `PlacementFailure`, `BadConfigError`,
  `DuplicateNameError`.
- `project_tab_ops.{hpp,cpp}` — `InfoView`, `ProfileView`, `AuxEntry`,
  `info_set/clear`, `profile_set/clear`, `aux_list/add/remove/export`,
  `embed_cover_image`, `clear_cover_image`, `sanitize_aux_name`, `is_png`,
  enum mappings for `AuxFolder`.
- `placement.{hpp,cpp}` — `plate_origin_offset` (sqrt grid across plates) and
  `place_in_plate` (within-plate sqrt grid with 10 mm margin).
- `png_placeholder.{hpp,cpp}` — 128×128 RGBA-0xC0 PNG generator;
  `plate_thumbnail_paths` canonical entry-name helper.
- `nanosvg_impl.cpp` — single-TU `#define NANOSVG_IMPLEMENTATION` so the
  vendor headers expand exactly once for libslic3r linkage.

CLI command-layer (`src/cli/commands/`, 12 files):
- `project_init.{hpp,cpp}` — `project init` + `project` parent subcommand.
- `project_tab.{hpp,cpp}` — `project info|profile|aux` subverbs.
- `plate.{hpp,cpp}` — `plate add|remove|rename|list`.
- `object.{hpp,cpp}` — `object add|remove|list|set-filament|split-to-parts
  |merge-parts`.
- `object_parse_vec3.{hpp,cpp}` — shared `--translate/--rotate/--scale`
  parser (rejects empty tokens, enforces arity).
- `config.{hpp,cpp}` — `config set|unset|list`.
- `inspect.{hpp,cpp}` — `inspect`.
- `mutation_runner.hpp` — header-only `MutationExceptionMap` registry +
  `run_mutation()` template; standard load-mutate-save envelope.

Vendored:
- `src/cli/cli11/CLI11.hpp` — CLI11 v2.4.2 single-header.

### orca-cli tests (42 new files, ~6,300 LOC)

- `tests/cli/CMakeLists.txt` — `cli_tests` target wiring, fixture paths via
  cache vars (`ORCA_CLI_REF_3MF`, `ORCA_CLI_STL_DIR`, `ORCA_CLI_FIXTURES_DIR`),
  `--order rand` discovery.
- `test_main.cpp`, `test_common.{cpp,hpp}` — Catch2 main + shared `run_cli()`
  helpers.
- `archive_invariants.{cpp,hpp}` — test-side zip-walking helpers.
- `unit/*.cpp` (10 files) — pure-function tests for `invariants`,
  `mutation_runner`, `object_parse_vec3`, `output`, `placement`,
  `png_placeholder`, `project_ops`, `project_tab_ops`, `split_fixtures`.
- `roundtrip/*.cpp` (7 files) — in-process load/mutate/save tests.
- `e2e/*.cpp` (9 files — `test_config`, `test_inspect`, `test_merge`,
  `test_object`, `test_plate`, `test_project_init`, `test_project_tab`,
  `test_smoke`, `test_split`) — child-process tests of the `orca-cli`
  binary.
- `fixtures/` — 6 committed STLs + `temp_project_for_orca_slicer.3mf` (113 KB
  reference), `cover_smoke.png/jpg`, `assembly_smoke.txt`, plus a
  `gen_minimal_stls.cpp` regenerator.

### Documentation (12 new files)

- `docs/cli/build.md` — CMake + Catch2 (v3.11.0 vs MSVC) build notes.
- `docs/cli/manual-test.md` — cumulative manual smoke-test recipe.
- `docs/cli/status.md` — phase tracker (v1 → v2 → cleanup → split → merge →
  project-tab; current status is "Phase 10 done").
- `docs/superpowers/specs/2026-05-19-orca-cli-design.md` — v2 design spec.
- `docs/superpowers/specs/2026-05-20-orca-cli-merge-parts-design.md`.
- `docs/superpowers/specs/2026-05-20-orca-cli-project-tab-design.md`.
- `docs/superpowers/specs/2026-05-20-orca-cli-split-to-parts-design.md`.
- `docs/superpowers/plans/2026-05-19-orca-cli.md` — 8-phase ~60-task TDD plan.
- `docs/superpowers/plans/2026-05-19-orca-cli-cleanup.md` — post-T15 cleanup.
- `docs/superpowers/plans/2026-05-20-orca-cli-merge-parts.md`.
- `docs/superpowers/plans/2026-05-20-orca-cli-project-tab.md`.
- `docs/superpowers/plans/2026-05-20-orca-cli-split-to-parts.md`.

---

## Per-Feature Detail

### orca-cli binary (umbrella)

- **Purpose**: A single new exe that loads a `.3mf`, mutates the in-memory
  `Model` + `DynamicPrintConfig` + `PlateData` via libslic3r's own structures,
  and writes the result back through libslic3r's `store_bbs_3mf` — without
  any wxWidgets / GUI dependency.
- **Entry point**: `src/cli/main.cpp:14` (`int main`). Registers global flags
  at `:21`, then five subcommand trees (`project`, `plate`, `object`,
  `config`, `inspect`) at `main.cpp:22-26`.
- **Key files & functions**:
  - `src/cli/main.cpp:14-39` — argv → CLI11 parse → callback → `std::exit`.
  - `src/cli/CMakeLists.txt:8` — `orca_cli_core` static lib (everything the
    tests can also link); `:44` — `orca-cli` exe; `:55-61` — Windows DLL
    mirror via `copy_directory_if_different`.
  - `src/cli/output.hpp:11` — `enum class ExitCode` (10 codes, 0-9).
  - `src/cli/globals.cpp:6` — registers `--json`, `--verbose`.
  - `src/cli/commands/mutation_runner.hpp:73` — `run_mutation<>` envelope.
- **Data flow**: argv → CLI11 dispatch → per-command callback → `run_mutation`
  → `load_project` (reads + rebuilds `objects_and_instances`) → mutator
  closure (operates on `ProjectState`) → `save_project` (`.tmp` write →
  invariants → atomic rename) → `print_ok/print_err` envelope → `std::exit`.
- **CLI surface**: global `--json`, `--verbose`, `--version`, per-mutating
  command `--output PATH`. Subcommand trees `project`, `plate`, `object`,
  `config`, `inspect` (see per-feature entries below).
- **Notable design choices**:
  - Vendored CLI11 (`src/cli/cli11/CLI11.hpp`) — single-header, no
    package-manager dep.
  - Linking: `orca_cli_core` is a static lib; both `orca-cli` and `cli_tests`
    link it, so tests exercise the exact same code as the shipped binary
    (`src/cli/CMakeLists.txt:48`, `tests/cli/CMakeLists.txt:52`).
  - Exit-code taxonomy is closed at 10 values (0-9). Each typed exception
    class maps to one code via `MutationExceptionMap`.
  - Per-subcommand `static` option storage so CLI11's by-reference binding
    keeps option values alive across parse → callback
    (`src/cli/commands/object.cpp:238`, etc.).

### `project init` — clone-and-mutate

- **Purpose**: Make a fresh `.3mf` from a known-good reference template,
  ensuring the result still passes archive invariants. v1 of the CLI tried
  to synthesize 3mfs from scratch and broke GUI compat; v2 pivoted to
  this approach.
- **Entry point**: `src/cli/commands/project_init.cpp:78`
  (`install_project_init_subcmd`); `:31` (`do_project_init`).
- **Key files & functions**:
  - `src/cli/commands/project_init.cpp:31-74` — copy `template` to `out.init-tmp`,
    `load_project`, `verify_input_template_thumbnails`, `save_project`, remove
    staging file.
  - `src/cli/io.cpp` (`load_project`, `save_project`).
  - `src/cli/invariants.cpp:verify_input_template_thumbnails` — refuses to
    load a template whose plate small-PNGs are missing.
- **Data flow**: `--template REF.3mf` → copy to `.init-tmp` → load via
  `load_bbs_3mf` (`LoadModel | LoadConfig`) → rebuild
  `PlateData::objects_and_instances` → invariant check on staging copy →
  store via `store_bbs_3mf` → atomic rename to `out`.
- **CLI surface**: `orca-cli project init <out> --template <ref> [--output X]`.
- **Notable design choices**:
  - Validate the **staging copy** but report against the user-supplied
    template path — closes a TOCTOU window where someone could swap the
    template between load and validate.
  - Staging file is removed in both happy and error paths.

### Plate CRUD

- **Purpose**: GUI parity for plate management without launching the GUI.
- **Entry point**: `src/cli/commands/plate.cpp:83` (`register_plate_subcmd`).
- **Key files & functions**:
  - `src/cli/project_ops.cpp::{add_plate, remove_plate, rename_plate}` — pure
    in-memory mutations on `ProjectState::plates`.
  - `src/cli/io.cpp` — orphan plate-PNG pruning regex (`thumbnail_re`,
    `is_orphan_plate_png`); placeholder PNG injection during save passthrough.
  - `src/cli/png_placeholder.cpp` — 128×128 RGBA 0xC0 PNG generator.
- **Data flow**: load → mutate `PlateData` vector (renumber `plate_index`
  contiguously after a remove) → save passthrough copies kept plate PNGs
  from the source archive and injects placeholders for new plates → invariants
  → rename.
- **CLI surface**: `plate add <file> --name N [--output O]`,
  `plate remove <file> --name N`, `plate rename <file> --from F --to T`,
  `plate list <file>` (rejects `--output` with `usage_error`).
- **Notable design choices**:
  - Plate thumbnails are keyed by `plate_index`, not name, so rename does
    NOT touch the PNG entries.
  - 0xC0 gray RGBA (not transparent) — matches a finding from the sister
    `bambu-cli` v1.2 work; transparent alpha caused some renderer paths to
    drop the thumbnail.

### Object CRUD + transforms + filament

- **Purpose**: Add an STL to a named plate with explicit transforms and an
  optional filament-slot assignment; remove an object by name; list objects
  (with `--plate P` filter).
- **Entry point**: `src/cli/commands/object.cpp:230` (`register_object_subcmd`);
  `:45` (`do_object_add`).
- **Key files & functions**:
  - `src/cli/commands/object.cpp:45` — `do_object_add`; uses
    `AddObjectRawOpts` (strings) → `parse_vec3` → `AddObjectParams` (typed).
  - `src/cli/project_ops.cpp::add_object` — load STL into `ModelVolume`,
    stamp `volume.source`, place `count` instances on the named plate.
  - `src/cli/project_ops.cpp::remove_object` — rebuilds every
    `PlateData::objects_and_instances` map and decrements indices.
  - `src/cli/placement.cpp` — `plate_origin_offset` + `place_in_plate`.
  - `src/cli/project_ops.cpp::set_object_filament` — stamp
    `extruder = filament_slot` (1-based, validated against
    `filament_settings_id`).
- **Data flow**: STL → `Model.add_object()` → set
  `volume.source.input_file = stl_path` (Bug C defense) → if any of
  `translate/rotate/scale` set, "stacking" branch places all `count` instances
  at the same point; otherwise per-plate sqrt grid placement; if `--filament S`
  set, `set_object_filament` stamps the slot. Off-bed result throws
  `PlacementFailure` → exit 9.
- **CLI surface**:
  - `object add <file> --plate P --stl S [--count N] [--name M]
    [--translate x,y[,z]] [--rotate ax,ay,az] [--scale s | sx,sy,sz]
    [--filament K] [--output O]`
  - `object remove <file> --name M [--output O]`
  - `object set-filament <file> --name M --filament K [--part V] [--output O]`
  - `object list <file> [--plate P]`
- **Notable design choices**:
  - `--stl` not `--file` — avoids a CLI11+MSVC `/GS` interaction that aborts
    when a long option name collides with a positional name
    (`object.cpp:265-268`).
  - "Explicit transform" branch (stacking) is captured as a separate code
    path in `has_explicit_transform()` (`project_ops.hpp:151`) — sibling
    parity with the deterministic grid path.
  - `--count N` writes **N independent `ModelObject`s**, not N instances of
    one object — recently retrofitted (commit `f7a2399f73`) to match how the
    GUI treats clones, after a roundtrip data-loss regression
    (`cdf7487c8c`).
  - Per-volume `--part` flag for `set-filament` was added in Phase 8 (split)
    so config edits can target a specific post-split volume
    (`object.cpp:324`).

### `object split-to-parts`

- **Purpose**: Decompose a single-volume object's mesh into one
  `ModelVolume` per connected component, in place.
- **Entry point**: `src/cli/commands/object.cpp:342`
  (`obj->add_subcommand("split-to-parts", ...)`).
- **Key files & functions**:
  - `src/cli/project_ops.cpp::split_object_to_parts` — wraps libslic3r's
    `ModelVolume::split` and walks the result to call `stamp_source_if_missing`
    on each new volume.
  - `src/cli/project_ops.cpp::stamp_source_if_missing` (declared at
    `project_ops.hpp:297`).
- **Data flow**: load → look up `ModelObject` by name → require exactly one
  `MODEL_PART` volume with >1 connected components → `split_volume` →
  rename + stamp source on each new volume → save.
- **CLI surface**: `object split-to-parts <file> --name N [--output O]`.
- **Notable design choices**:
  - Object identity (name, plate, instance count, instances) is preserved
    — only the `ModelVolume` list changes.
  - Source-file stamping (`stamp_source_if_missing`) is the "Bug C defense":
    some Orca/Bambu GUI builds silently drop a part whose
    `source.input_file` is empty.

### `object merge-parts`

- **Purpose**: Inverse of split — combine a chosen subset of an object's
  volumes back into one merged volume.
- **Entry point**: `src/cli/commands/object.cpp:230+` (registered alongside
  the other object subverbs; the merge-parts subcommand block starts where
  `merge_filament`/`merge_parts` statics are declared at `:258`).
- **Key files & functions**:
  - `src/cli/project_ops.cpp::merge_object_parts` (declared
    `project_ops.hpp:341`) — validation pipeline + `TriangleMesh::merge`.
  - `src/cli/project_ops.hpp::DuplicateNameError` — maps to exit 5.
- **Data flow**: validate (object exists, every source name is a
  `MODEL_PART` volume on that object, `--filament` in range, no naming
  collision with non-source volumes, ≥2 non-empty sources, per-volume
  non-extruder config keys agree across sources OR are accepted via
  `filament_override`) → bake each source's `get_matrix()` into its mesh →
  `TriangleMesh::merge` triangle-concatenation → insert merged volume at
  the lowest-existing-index slot among sources → erase the others →
  identity matrix on the merged volume → save.
- **CLI surface**: `object merge-parts <file> --name N --parts a,b,c
  --into M [--filament S] [--output O]`.
- **Notable design choices**:
  - "Lowest-existing-index" placement makes the result invariant under
    reordering of `--parts`.
  - Strict per-volume config-agreement rule (rejects with
    `invalid_state`/exit 7) — easier to relax later than to retrofit.
  - `--filament` parsed via `count("--filament") > 0`, not a sentinel, so
    `--filament 0` correctly reaches the range check
    (`object.cpp:252-259`).

### `config set/unset/list`

- **Purpose**: Edit `DynamicPrintConfig` keys (project-wide or per-object)
  via libslic3r's own parsers, with no GUI in the loop.
- **Entry point**: `src/cli/commands/config.cpp:188` (`register_config_subcmd`).
- **Key files & functions**:
  - `src/cli/commands/config.cpp:60` (`do_config_set`), `:82` (`do_config_unset`),
    `:110` (`do_config_list`).
  - `src/cli/project_ops.cpp::{set_project_config, set_object_config,
    unset_project_config, unset_object_config, changed_project_keys,
    object_config_keys, filament_slot_count}` — all share `print_config_def`
    validation; values flow through libslic3r's `set_deserialize`.
- **Data flow**: validate key against `print_config_def` (`BadConfigError`
  on miss) → `set_deserialize` parses the value → for `--object`, write to
  the named `ModelObject`'s `ModelConfigObject` AND mark the key in
  `different_settings_to_system` so the GUI's "modified" badge picks it up
  (commit `76bc42d42c`) → save → invariant guard `verify_vector_config_roundtrip`
  catches any save/load asymmetry for vector-typed options.
- **CLI surface**:
  - `config set <file> --key K --value V [--object N] [--output O]`
  - `config unset <file> --key K [--object N] [--output O]`
  - `config list <file> [--object N] [--changed-only]` (rejects `--output`).
- **Notable design choices**:
  - Vector configs go through libslic3r's per-option deserializer (with its
    own `,`/`;`/`#` separator) — no per-key parsing in the CLI.
  - `--changed-only` uses `DynamicPrintConfig::diff(new_from_defaults_keys)`
    rather than `serialize()` on the default value (the latter crashes on
    `coEnum`s).
  - `--object N` adopts "group-by-name" semantics so `--count N` clones
    (which are now separate `ModelObject`s) are updated together — recently
    retrofitted (commits `c2ddf51d87`, `fa9ec76e2c`, `b32bccdfb3`).

### `inspect`

- **Purpose**: Diagnostic dump of a loaded `.3mf` — model objects, plate
  assignments, per-volume extruder, project_config snapshot.
- **Entry point**: `src/cli/commands/inspect.cpp:158`
  (`register_inspect_subcmd`); `:30` (`do_inspect`).
- **Key files & functions**:
  - `src/cli/commands/inspect.cpp:30` — walks `Model`, `plates`,
    `project_config`, calls `object_volume_info` per object.
  - `src/cli/project_ops.cpp::object_volume_info` (declared
    `project_ops.hpp:269`).
- **Data flow**: load → walk Model objects → walk PlateData → emit either
  human-readable lines or one JSON envelope. Read-only; `--output` is
  rejected with `usage_error`.
- **CLI surface**: `inspect <file>` (no mutating flags).

### `project info {show,set,clear}`

- **Purpose**: Edit `ModelInfo` (project tab in the GUI): title, description,
  license, copyright, cover image.
- **Entry point**: `src/cli/commands/project_tab.cpp:193`
  (`install_project_info_subcmd`); `do_info_show/set/clear` at `:26/:73/:89`.
- **Key files & functions**:
  - `src/cli/project_tab_ops.cpp::info_view` — read.
  - `src/cli/project_tab_ops.cpp::info_set` — write (allocates `model_info`
    if null); routes `--cover` through `embed_cover_image`.
  - `src/cli/project_tab_ops.cpp::info_clear` — nulls each named field;
    `cover` routes through `clear_cover_image` (refcount).
  - `src/cli/project_tab_ops.cpp::embed_cover_image` — `is_png` magic check →
    copy to `<aux_temp>/.thumbnails/thumbnail_3mf.png` → set
    `cover_file = "Auxiliaries/.thumbnails/thumbnail_3mf.png"`.
- **Data flow**: load → mutate `model.model_info` → optionally embed/clear
  the cover image blob → save.
- **CLI surface**:
  - `project info show <file>`
  - `project info set <file> [--title T] [--description D] [--license L]
    [--copyright C] [--cover IMG.png] [--output O]`
  - `project info clear <file> --field F [--field F]... [--output O]`
    (valid field names: see `allowed_info_fields()`).
- **Notable design choices**:
  - Cover image is **PNG only** — `BadCoverImage` (exit 4) on non-PNG, with
    the magic check done via `is_png` (first 8 bytes match the PNG
    signature).
  - Cover-image file is shared with the profile cover — see refcount note
    below.

### `project profile {show,set,clear}`

- **Purpose**: Edit `ModelProfileInfo` (the second tab in the GUI's project
  metadata): profile title, description, cover. `user_id` and `user_name`
  are read-only.
- **Entry point**: `src/cli/commands/project_tab.cpp:247`
  (`install_project_profile_subcmd`); `do_profile_show/set/clear` at
  `:126/:167/:182`.
- **Key files & functions**: `profile_view`, `profile_set`, `profile_clear`
  in `project_tab_ops.cpp`. Same `embed_cover_image` / `clear_cover_image`
  routing as `info`, with `CoverTarget::Profile`.
- **Data flow**: same shape as `info`. The two surfaces share a single
  on-disk blob (`Auxiliaries/.thumbnails/thumbnail_3mf.png`); clearing one
  nulls only the corresponding pointer and deletes the file **only** when
  the other surface's pointer is also empty (`clear_cover_image`,
  `project_tab_ops.hpp:223`).

### `project aux {list,add,remove,export}`

- **Purpose**: Manage the auxiliary file buckets shown in OrcaSlicer's
  `AuxiliaryPanel` (pictures / bom / assembly-guide / others).
- **Entry point**: `src/cli/commands/project_tab.cpp:420`
  (`install_project_aux_subcmd`); `do_aux_list/add/remove/export` at
  `:304/:341/:365/:379`.
- **Key files & functions**:
  - `src/cli/project_tab_ops.cpp::aux_list` — walks four bucket subdirs
    under `model.get_auxiliary_file_temp_path()`.
  - `src/cli/project_tab_ops.cpp::aux_add` — sanitize name → copy →
    handle `--force` and collisions.
  - `src/cli/project_tab_ops.cpp::aux_remove` / `aux_export` —
    `out_of_range` if missing; export accepts both file and directory
    destinations.
  - `src/cli/project_tab_ops.cpp::sanitize_aux_name` — rejects path
    separators, dot-prefix/suffix, CON/PRN/AUX/NUL/COM1-9/LPT1-9 (with or
    without extension).
- **Data flow**: load → identify bucket subdir → fs ops on the model's
  auxiliary temp dir → save (3mf packaging picks up the file).
- **CLI surface**:
  - `project aux list <file>`
  - `project aux add <input> --folder F --file PATH [--name N] [--force]
    [--output O]`
  - `project aux remove <file> --folder F --name N [--output O]`
  - `project aux export <file> --folder F --name N --to PATH`
- **Notable design choices**:
  - Folder enum has three string forms: CLI flag (`hyphen-form`), JSON key
    (`underscore_form`), in-3mf subdir (matches the GUI's `Auxiliary.cpp`
    naming exactly).
  - `aux_list` always returns all four keys (empty vectors for empty
    buckets) — stable JSON shape for scripted consumers.
  - `--force` on a byte-identical file is treated as a no-op success.

### Archive invariant guards

- **Purpose**: Guard against the class of bugs where a save produces a
  GUI-incompatible 3mf even though every per-call check passed. Failure
  causes the destination to remain untouched (the `.tmp` file is not
  renamed) and surfaces `ExitCode::invariant_violation`.
- **Entry point**: `src/cli/io.cpp::save_project` (calls
  `run_all_invariants` before the rename).
- **Key files & functions**:
  - `src/cli/invariants.cpp::run_all_invariants` (`invariants.hpp:50`).
  - `verify_relationships` — every `Target=` in any `.rels` entry must
    resolve to an in-archive name (catches "Bug B" — dangling references).
  - `verify_plate_thumbnails` — for every `Metadata/plate_N.png` there must
    be a `Metadata/plate_N_small.png`.
  - `verify_vector_config_roundtrip` — vector-typed `project_config` keys
    must serialize, store, re-load, and re-serialize byte-identically
    (catches the class of bug that lost `wipe_volume_matrix` in v1 of the CLI).
  - `verify_input_template_thumbnails` — standalone variant used by
    `project init` to refuse a broken template.
- **Data flow**: after `store_bbs_3mf` writes `target.tmp` → reopen as a
  zip in memory → run the three checks → on success, atomic rename
  `target.tmp → target`; on failure, `.tmp` is removed in the caller's
  catch block (`run_mutation`).
- **CLI surface**: not user-facing; surfaces as exit 8 + message.

### `print_ok` / `print_err` envelope

- **Purpose**: Single output formatter shared by every subcommand; JSON or
  plaintext switched by `--json`.
- **Entry point**: `src/cli/output.cpp::{print_ok, print_err,
  build_ok_envelope, build_err_envelope}`.
- **Data flow**: `--json` off → write `ok: <message>` / `err[<code>]: <msg>`
  to stdout/stderr; `--json` on → emit a one-line `nlohmann::json` envelope.
- **Notable design choices**:
  - `emit_list_response<>` template (`output.hpp:39`) factors out
    "ls"-style listings so every `list` subcommand renders identically.
  - All escaping is done by `nlohmann::json` — the original hand-rolled
    `escape_json` was dropped in commit `06e16d4837` (cleanup M1).

### `MutationExceptionMap` + `run_mutation`

- **Purpose**: Single load-mutate-save envelope shared by every mutating
  subcommand, with a typed exception-to-exit-code registry.
- **Entry point**: `src/cli/commands/mutation_runner.hpp:73` (`run_mutation`);
  `:19` (`MutationExceptionMap`).
- **Data flow**: caller registers `.on<T>(ExitCode::X)` predicates in
  registration order → `run_mutation` runs `check_input_exists` →
  `resolve_save_target` → tries the mutator closure inside a try/catch →
  `InvariantViolation` always maps to exit 8; otherwise the map's predicates
  are tried via `dynamic_cast`; fallthrough is `std::invalid_argument →
  duplicate_name`, `std::out_of_range → unknown_reference`, anything else
  → `parse_failure`.
- **Notable design choices**:
  - Header-only template — every command's callback inlines the body, so
    typed exception classes (`PlacementFailure`, `BadConfigError`,
    `DuplicateNameError`, `BadCoverImage`, etc.) get their own targeted
    mapping per command instead of one global table.

---

## Integration Points with Upstream Code

### Modified upstream files (CLI infrastructure only)

- `src/CMakeLists.txt:15` — single `add_subdirectory(cli)` (no other change).
- `tests/CMakeLists.txt:52` — single `add_subdirectory(cli)` (no other change).
- `README.md:69` — added the orca-cli paragraph; no upstream prose touched.

### Modified upstream files (cherry-picks from upstream PRs that landed on origin/main)

These five PRs were not part of the merge-base but are part of HEAD; they touch
upstream files and have no relationship to the CLI. They are listed here for
exhaustiveness and because the same five PRs may or may not be in the parallel
BambuStudio fork.

- **PR #13575** ("Ignore Jerk values Warning if using JD") — commit
  `bbeb9d5ca5` — modifies `src/libslic3r/Print.cpp:1754` to wrap the jerk
  validation block in `if (!ignore_jerk_validation)` and to hoist the
  `max_junction_deviation` lookup above the jerk check.
- **PR #13720** ("Fix Italian translation") — commit `8371d78696` — refines
  scarf-seam translation strings in `localization/i18n/it/OrcaSlicer_it.po`.
- **PR #13738** ("feat: add UI feedback on http error and some logs") —
  commit `ec0cb2552b` — `GUI_App.cpp:4849+` adds 410/4xx handling and an
  `Orca Cloud API Error` dialog; renames `m_show_http_errpr_msgdlg` →
  `m_show_http_error_msgdlg` (`GUI_App.hpp:327`); promotes
  `ImGuiWrapper::display_initialized()` to public so the gate can be checked
  from outside (`ImGuiWrapper.hpp:376`).
- **PR #13741** ("Fix first layer toolhead preheating bug…") — commit
  `9417156f06` — `GCodeProcessor.cpp:1233+` adds a locally-tracked
  `current_layer_id` counter that's incremented on each `;LAYER_CHANGE`
  marker and used to gate first-layer vs. other-layer nozzle temps for
  pre-heat M104 inserts.
- **PR #13747** ("Fix Linux/Wayland crash on Preview tab at startup") —
  commit `f0aebec380` — `GUI_App.cpp:787+` inverts the
  `IsShownOnScreen && make_current_for_postinit` branch and, on Linux,
  bails out of `post_init` (with `m_post_initialized = false`) so the
  Wayland/EGL surface gets another chance to commit before any GL call.

### Architecture: wrap, don't fork

- `libslic3r` is **linked as a static library** from `orca_cli_core`
  (`src/cli/CMakeLists.txt:33`). No libslic3r source is forked or
  duplicated inside `src/cli/`.
- All `.3mf` IO goes through libslic3r's own `load_bbs_3mf` / `store_bbs_3mf`
  (`src/cli/io.cpp:6` includes `Format/bbs_3mf.hpp`). The CLI never writes
  its own XML/zip emitters; placeholder PNGs are injected only at the
  thumbnail-passthrough layer, not by re-implementing the 3mf writer.
- All config validation flows through `print_config_def` + the same
  `set_deserialize` the GUI calls — see `project_ops.hpp:204-211`.
- Mesh ops use libslic3r's own `TriangleMesh::merge`, `ModelVolume::split`,
  `Model::add_object` rather than reimplementing them.
- **No monkey-patching** of upstream classes. **No conditional compilation
  flags** introduced. **No shim layer** between `orca_cli_core` and
  libslic3r. The CLI is a strict consumer.
- One small upstream-side surface widening (PR #13738) makes
  `ImGuiWrapper::display_initialized()` public; otherwise upstream classes
  are unchanged.

---

## Open Questions / Rough Edges

- **Not pushed.** Memory notes the v2 work as "shipped locally on `main`
  2026-05-19" but `origin/main` was last pushed to
  `8df506006a` (project-tab Phase 10 wrap-up). The 12 commits after that
  — `cdf7487c8c`..`a7748fbd20`, including the recent `--count` /
  `group-by-name` retrofit — are local only. Worth flagging before
  cross-comparing because the BambuStudio sibling may already include
  equivalent fixes.
- **Untracked working-tree files**: `baseline_junit.xml`, `final_junit*.xml`,
  `fix_junit.xml`, `phaseA_junit.xml`, `t10_stdout.txt`, `full_suite_*.txt`,
  `suite_stdout.txt`, `suite_stderr.txt`, `t_orca_cli.xml`, plus
  `docs/superpowers/plans/2026-05-20-orca-cli-cross-project-audit.md` and
  `docs/superpowers/plans/2026-05-21-orca-cli-sibling-audit-v2.md` — none
  of these are checked in.
- **Strict per-volume config-agreement** in `merge-parts`: spec § 3.5 calls
  it deliberately strict ("easier to relax later than to retrofit"). If the
  BambuStudio sibling has a looser rule, that's a known divergence rather
  than a bug on either side.
- **`--object` group-by-name semantics retrofit** (recent commits
  `c2ddf51d87` / `fa9ec76e2c` / `b32bccdfb3` / `f7a2399f73`) — landed for
  `remove`, `set-filament`, `config set/unset/list`, but not all object
  subcommands have been audited end-to-end against the new semantics. The
  cross-project audit plan in the working tree (`2026-05-20-orca-cli-
  cross-project-audit.md`) is the in-flight tracker for this.
- **Cross-plate `identify_id` uniqueness**: commit `801f660333` added a
  failing regression test ("audit item 2"). Confirm the fix landed on top
  of it before counting it as closed.
- **`source_path` lifetime contract** on `ProjectState` (`project_ops.hpp:62`)
  — if the source `.3mf` is deleted between load and save, the thumbnail
  passthrough quietly produces an archive that the runtime invariant
  guard then rejects with a dangling-relationship error. Loud-failure
  surface, but the failure mode is one removed from "passed to save_project
  with a stale path."
- **CLI11 + MSVC option-name collision** (`object.cpp:265-268`): the
  workaround is the long-option rename (`--stl` not `--file`). If the
  parallel Bambu fork uses a different CLI library, this is a one-sided
  papercut to bear in mind when comparing surfaces.

---

Report file: `C:\Users\ildarcheg\AppData\Local\Temp\orca_slicer_changes_report.md`
