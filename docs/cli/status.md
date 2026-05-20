# orca-cli - implementation status

Updated by each phase.

## Phase 0 - Scaffold

- [x] `orca-cli` builds.
- [x] `cli_tests` builds with smoke tests green.
- [ ] Manual GUI smoke (none required for P0; binary just prints --version).

## Phase 1 - `project init`

- [x] `load_project` reads the reference 3mf into a `ProjectState`
      (`Model` + `DynamicPrintConfig` + `PlateData[]`) with the
      plate <-> instance index rebuilt from `loaded_id`.
- [x] `save_project` writes via a `.tmp` + atomic rename, including a
      thumbnail blob passthrough from the originating archive so
      `_rels/.rels` never points at a missing PNG.
- [x] All three runtime invariant guards (relationships, plate small
      thumbnails, vector-config roundtrip) pass on the reference 3mf
      and are wired into `save_project` to fail the write on
      regression.
- [x] `orca-cli project init <out> --template <ref>` exits 0 and the
      produced archive passes the e2e archive checks (4-point bed,
      `<part>` source_file presence, 128px small thumbnails).
- [ ] Manual GUI smoke: opens cleanly in OrcaSlicer with the same
      plates and objects as the reference. (Pending separate manual
      verification per `docs/cli/manual-test.md` -> Phase 1.)

## Phase 2 - plate ops

- [x] `add_plate`, `remove_plate`, `rename_plate` mutations on
      `ProjectState`, with unit tests covering happy paths and
      every documented failure mode (duplicate name, missing name,
      "only plate" guard, idempotent rename).
- [x] `make_placeholder_png_128_gray_C0()` produces a 128x128 RGBA
      PNG via hand-rolled chunk emission + `mz_compress2` zlib
      stream. Used to fill in the thumbnail PNG entries for plates
      that have no source-archive blob to copy through.
- [x] `passthrough_missing_thumbnails` now injects placeholder
      PNG bytes for any plate whose `Metadata/plate_<n>.png` or
      `plate_<n>_small.png` is missing from both the source archive
      and the freshly-stored .tmp archive. Keeps the runtime
      invariant guards happy for `plate add`.
- [x] `orca-cli plate {add,remove,rename,list} <file> [--output <out>]`
      end-to-end via the standard load -> mutate -> save flow.
      `plate list` rejects `--output` with `usage_error`.
- [x] `--output <side-car>` round-trip: input file remains
      byte-identical (size + mtime); the new plate appears only in
      the side-car output.
- [ ] Manual GUI smoke: $OUT and $OUT-side both open in OrcaSlicer
      with the new plates visible and named correctly in the plate
      switcher. (Pending separate manual verification per
      `docs/cli/manual-test.md` -> Phase 2.)

## Phase 3 - object add (auto-place)

- [x] `placement.cpp` provides two pure helpers (`plate_origin_offset`
      and `place_in_plate`) implementing the GUI's sqrt-based 2D plate
      grid and a deterministic within-plate row/column slot layout.
      Unit-tested on its own without any IO / Model dependencies.
- [x] `add_object` in `project_ops.cpp` loads an STL via `load_stl`,
      appends a fresh `ModelObject` (deep-copied so the scratch model
      can be discarded), stamps `ModelVolume::source.{input_file,
      object_idx, volume_idx}` on every volume (Bug C defense -- spec
      section 8), and places `count` instances on the named plate via
      `plate_origin_offset` composed with `place_in_plate`. The reference
      printable_area drives the per-plate stride; falls back to a
      250 x 250 default if missing.
- [x] `remove_object` in `project_ops.cpp` removes a `ModelObject` by
      name and rebuilds every `PlateData::objects_and_instances` so
      entries pointing at the removed object are dropped and indices
      greater than the removed index are shifted down by 1.
- [x] `orca-cli object {add,remove,list} <file> [--output O]` end-to-end.
      `add` accepts `--plate`, `--stl`, `--count`, `--name`. `list`
      rejects `--output` with `usage_error`. Subcommand option name is
      `--stl` (not `--file`) to avoid the CLI11 + MSVC /GS abort we
      hit on G9.
- [x] Translation / rotation / scale flags are deliberately not in P3
      -- they are P4's responsibility. `--filament` is P5.
- [x] e2e: object add produces an archive that passes
      `assert_parts_have_source_file` (source_file metadata present on
      every `<part>` in `Metadata/model_settings.config`).
- [ ] Manual GUI smoke: open the P3 manual-test output in OrcaSlicer
      and verify 2 new ModelObjects on `Brackets` -- `cyl` (1 instance)
      and `cone` (3 instances) -- all 4 instances render and slice.
      (Pending separate manual verification per
      `docs/cli/manual-test.md` -> Phase 3.)
- [Known limitation] `orca-cli {plate,object} list` displays 0 objects
      per plate even on the unmodified reference 3mf. This is a
      pre-existing `load_project` / `loaded_id` rebuild gap and not a
      P3 regression -- the on-disk `objects_and_instances` are written
      correctly by `add_object` -> `save_project` (OrcaSlicer loads
      them fine). Fix scheduled for a later phase.

## Phase 4 - object transforms

- [x] `AddObjectParams` extended with `std::optional<Vec3d>` translate /
      rotate / scale, plus a `has_explicit_transform()` helper. The
      `--translate` flag is plate-local (0,0 == plate's bed-min corner)
      and folds in the per-plate origin offset that P3 already applies
      to grid placement.
- [x] `parse_vec3` in `commands/object.cpp` accepts 2-component
      (z=0 default) and 3-component vectors; `--scale` additionally
      accepts a uniform scalar `s` which expands to `{s,s,s}`. Each
      flag is captured as an optional string so CLI11's "unset" state
      is preserved; bad values report `usage_error` (exit 1) before
      the archive is touched.
- [x] `add_object` instance loop branches on `has_explicit_transform`:
      when any transform is supplied, all `--count N` instances stack
      at the same post-transform position (spec § 4.3); otherwise the
      deterministic per-plate grid from P3 fires. Both branches apply
      the per-plate origin offset, so a stack on plate N still lands
      at world `(plate_origin + local_translate)`.
- [x] Rotation and scale are applied per-instance via
      `ModelInstance::set_rotation(Vec3d)` and
      `ModelInstance::set_scaling_factor(Vec3d)`. The same transform
      is copied onto every stacked instance so they're identical
      regardless of which one the GUI picks as canonical. Per-instance
      is the cleaner choice because libslic3r's `ModelObject::rotate`
      and `ModelObject::scale` are destructive (they alter the mesh)
      and have no Vec3d "set" form.
- [x] Off-bed guard: when an explicit transform is supplied, the
      post-scale world-space AABB is checked against the target
      plate's bed (X/Y). Failure throws `PlacementFailure` (a dedicated
      exception type in `project_ops.hpp`); the command callback maps
      it to `ExitCode::placement_failure` (exit 9) and prints the
      offending local offset in the message. The catch order in
      `do_object_add` places `PlacementFailure` before
      `std::out_of_range` so off-bed is not misreported as
      `unknown_reference`.
- [x] P4 e2e: 7 new tests in `tests/cli/e2e/test_object.cpp`
      cover single-instance translate, `--count N` stacking,
      grid-fallback when no transform, off-bed exit-9, per-axis scale,
      Z-axis rotation, and bad `--translate` -> usage_error. All
      `[orca-cli][P3]` tests still pass (regression check).
- [ ] Manual GUI smoke: open the P4 manual-test output in OrcaSlicer
      and verify 3 cubes on plate T -- `plain` at (60,60), `big`
      (--scale 2) at (120,60), `spun` (--rotate 0,0,0.7854) at
      (90,120) -- all visible and slicing cleanly. (Pending separate
      manual verification per `docs/cli/manual-test.md` -> Phase 4.)

## Phase 5 - object filaments

- [x] `AddObjectParams` extended with `std::optional<int> filament_slot`.
      When set, `add_object` delegates to `set_object_filament` after
      instance placement so the validation path is identical between
      `object add --filament` and `object set-filament`.
- [x] `set_object_filament(state, name, slot)` in `project_ops.cpp`
      stamps `extruder = slot` on the named ModelObject's
      `ModelConfigObject` (same call shape used by libslic3r's MMU
      gizmo and `GUI_ObjectList`). Validates `1 <= slot <=
      filament_settings_id.size()` and throws `std::out_of_range`
      on either out-of-range slot OR unknown object -- the command
      layer maps both to `ExitCode::unknown_reference` (exit 6).
- [x] `--filament N` flag added to `object add`; new
      `object set-filament <file> --name M --filament N [--output O]`
      subcommand registered in `commands/object.cpp`. Slot 0 is the
      CLI11 "unset" sentinel on `object add` (1-based, so any real
      slot is >=1); explicit 0 / negative values forwarded to the
      validator surface the same exit-6 error a user would see from
      `set-filament`.
- [x] e2e: `archive_invariants::assert_object_extruder` activated.
      Bug C lock-in: `object add --filament 2 ... && assert_object_
      extruder(in, "cube_f2", 2) && assert_parts_have_source_file(in)`
      runs in the same test case so source_file presence is pinned
      against any future regression that would drop it on the
      filament-assigned object. Roundtrip test confirms the
      per-object `extruder` value survives load/save.
- [x] All P0-P4 tests still pass (regression check).
- [ ] Manual GUI smoke: open the P5 manual-test output in OrcaSlicer
      and verify `cube1` is on filament slot 1 and `cube2` on slot 2
      in the object panel; both render and slice normally on plate F.
      (Pending separate manual verification per
      `docs/cli/manual-test.md` -> Phase 5.)

## Phase 6 - config

- [x] New `BadConfigError` exception in `project_ops.hpp` so unknown
      / malformed config keys map to `ExitCode::bad_config` (exit 4)
      instead of being misreported as `duplicate_name` by the
      `std::invalid_argument` mapping the plate command catch-chain
      uses.
- [x] `set_project_config` / `set_object_config` in `project_ops.cpp`
      validate `key` against `print_config_def`, then delegate the
      value parse to libslic3r's `set_deserialize` with a Disable
      substitution context. Strict parsing: a value libslic3r rejects
      (out-of-range int, malformed enum, ...) surfaces as
      `BadConfigError` -> exit 4 rather than silently falling through
      to a default.
- [x] `unset_project_config` / `unset_object_config` remove a key
      after the same `print_config_def` validation. Erasing an
      already-unset key is a no-op (matches the GUI's "reset to
      default" affordance), but a typo on the key name surfaces as
      `BadConfigError` -> exit 4.
- [x] `changed_project_keys` builds a defaults config via
      `DynamicPrintConfig::new_from_defaults_keys` restricted to the
      project's actual key set, then runs `DynamicConfig::diff` to
      pick out the differ-from-default keys. Spec G6 -- this avoids
      `default_value->serialize()` which crashes on `coEnum` defaults.
- [x] `object_config_keys` returns the explicitly-set keys on a
      ModelObject's `ModelConfigObject`. The per-object config only
      ever stores set keys (the GUI populates it lazily as the user
      touches settings), so this IS the change-set -- no diff needed.
- [x] `orca-cli config {set,unset,list} <file> [--key K] [--value V]
      [--object N] [--output O] [--changed-only]` end-to-end via the
      standard load -> mutate -> save flow. `set` and `unset` accept
      `--output` for the side-car round-trip; `list` rejects
      `--output` with `usage_error` (exit 1).
- [x] Vector-typed keys (`coPoint*`, `coStrings`, ...) pass straight
      through to libslic3r's per-option deserializer; no separator
      translation in v2's code path (the Bug A path was the v1.x
      `apply_preset_kvs` flow which v2 doesn't have). The vector-
      config roundtrip invariant guard from P1 catches any regression
      where the deserialized form doesn't byte-identically round-trip
      back through the serializer.
- [x] Catch chain in `commands/config.cpp` maps `BadConfigError` ->
      exit 4 (bad_config) and `std::out_of_range` -> exit 6
      (unknown_reference). The `BadConfigError` catch sits BEFORE
      the generic `std::exception` fallback so a typo on the key name
      doesn't get bucketed as `parse_failure`.
- [x] JSON `list` output is built via `nlohmann::json` so all
      user-controlled strings (keys and values) are escaped
      automatically; a config value containing quotes / backslashes /
      control characters can't break the surrounding `keys` array
      object.
- [x] e2e: 12 new tests in `tests/cli/e2e/test_config.cpp` cover
      project-level set, per-object set, unknown key (exit 4), bad
      value (exit 4), unknown object (exit 6), unset, unknown-key
      unset (exit 4), project list, `--changed-only` list (G6 smoke),
      `--output` on list (exit 1), per-object list, and the
      `--output` side-car round-trip. Plus 10 new unit tests covering
      the project_ops layer directly.
- [x] All P0-P5 tests still pass (regression check). Test count
      moved from 79 cases / 65974 assertions (baseline) to 101 cases
      / 66020 assertions.
- [ ] Manual GUI smoke: open the P6 manual-test output in OrcaSlicer
      and verify `cubeC` shows `wall_loops = 4` in the per-object
      settings panel and the global process-settings panel shows
      `sparse_infill_density = 30%`. (Pending separate manual
      verification per `docs/cli/manual-test.md` -> Phase 6.)

## Phase 7 - docs finalize + `inspect`

- [x] New read-only `orca-cli inspect <file>` subcommand in
      `commands/inspect.cpp`, wired into `main.cpp` and the
      `orca_cli_core` CMake target. Reports plate count, per-plate
      object names, filament slot count, project-level changed config
      keys (via `changed_project_keys` -- the same G6-safe path P6
      added for `config list --changed-only`), and the explicitly-set
      per-object config keys for every `ModelObject` in the loaded
      project. Read-only: rejects `--output` with `usage_error`
      (exit 1) the same way `plate list` / `object list` /
      `config list` do.
- [x] Human-mode output: one line per fact, indented to surface the
      per-plate / per-object grouping. JSON-mode output: a structured
      `data` object with `plate_count`, `filament_count`,
      `plates: [{index,name,objects:[...]}, ...]`,
      `project_changed: [...]`,
      `objects: [{name, config_keys: [...]}, ...]`. Every
      user-controlled string (plate names, object names, key names)
      is escaped automatically by `nlohmann::json` so a value
      containing quotes / backslashes / control characters can't
      break the surrounding JSON.
- [x] e2e: 5 new tests in `tests/cli/e2e/test_inspect.cpp` cover the
      human-mode happy path on the reference 3mf, the JSON-mode
      structured output, `--output` rejection (exit 1), missing-file
      (exit 2), and a mutation round-trip (plate add + object add +
      config set followed by `--json inspect` containing the new
      plate / object / config key).
- [x] Cumulative manual-test recipe appended to
      `docs/cli/manual-test.md` -- exercises the full P0..P7 chain
      (init -> plate add/rename -> object add with transforms +
      filaments -> config set both scopes -> inspect) in a single
      block.
- [x] `README.md` updated with a brief "orca-cli (experimental
      command-line composer)" section pointing at
      `docs/cli/manual-test.md` and `docs/cli/status.md`.
- [x] All P0-P6 tests still pass (regression check). Test count moved
      from 104 cases / 66028 assertions (P6 baseline) to 109 cases /
      66046 assertions.
- [x] Manual GUI smoke: cumulative P7 recipe (5 plates, 3 grid-placed
      object batches incl. `--count`, 2 explicit-transform objects,
      project + per-object config overrides) opened in OrcaSlicer
      and visually verified on 2026-05-20. Verified after the P3
      stride fix below; the same recipe before the fix put
      grid-placed objects in the inter-plate gutter.

Status: P0-P7 implementation complete. Phase-by-phase manual GUI
smokes remain pending (the unchecked items above), but every phase
has an automated e2e suite locking in the corresponding behavior and
the cumulative P7 recipe exercises the full surface area in a single
flow.

## Cleanup pass (2026-05-19)

HIGH + MEDIUM findings from a post-v2 review of `src/cli/**`, addressed in 15
incremental commits on `main`:

- T1 / M3 — `check_input_exists` centralized in `io.hpp`/`io.cpp`; four
  subcommand TUs no longer carry their own copies with drifting error
  messages ("input not found" is now the uniform wording).
- T2 / M9 — `find_object` and `find_object_or_throw` (const + non-const)
  promoted to `project_ops.hpp`; five inline lookup loops eliminated.
- T3 / M10 — `filament_slot_count` promoted from `inspect.cpp` into
  `project_ops`; the older anonymous `filament_count_of` renamed and
  unified with it.
- T4 / M7 — `plate_thumbnail_paths(int)` helper returns the five canonical
  `Metadata/plate_*` PNG names; hardcoded literals across `io.cpp` and
  `invariants.cpp` consolidated.
- T5 / H4 — `read_printable_area_aabb` replaced the hand-rolled min/max
  loop with libslic3r's `BoundingBoxf(const std::vector<Vec2d>&)` ctor.
- T6 / M5 — `parse_vec3` extracted to `commands/object_parse_vec3.{hpp,cpp}`
  and now returns `ParsedVec3 { values, component_count }`; the `--rotate`
  and `--scale` arity checks moved into the parser instead of the caller
  re-counting commas.
- T7 / M4 — `do_object_add`'s 10-parameter signature replaced by an
  `AddObjectRawOpts` struct constructed once by the CLI11 callback.
- T8 / H1 — `MutationExceptionMap` + `run_mutation` (new
  `commands/mutation_runner.hpp`) unify the load -> mutate -> save -> map
  exception -> print_ok envelope; every mutating subcommand (`plate
  {add,remove,rename}`, `object {add,remove,set-filament}`, `config
  {set,unset}`) routes through it. Exit-code drift fixed at the source.
- T9 / M1 — All CLI JSON output (`print_ok`, `print_err`, list / inspect
  responses) built via `nlohmann::json`; the hand-rolled `escape_json`
  deleted. E2E tests converted from substring-grep to
  `nlohmann::json::parse` + field assertions.
- T10 / M2 — `emit_list_response<Row, ToJson, ToLine>` template in
  `output.hpp` unifies the human-vs-JSON branching of `do_*_list`. The
  three list subcommands now build a `vector<Row>` and call the helper.
- T11 / M11 — `save_project` adopts the safer rename-to-`.bak` /
  rename-in / remove-`.bak` swap pattern, matching what
  `atomic_swap_rewrite` already did. The destination is never absent.
- T12 / M8 — `enumerate_zip_entry_names` and `extract_entry_to_memory`
  replace `unzip_to_memory` for invariant checks that only need entry
  names or a single `.rels` file. Peak memory on `run_all_invariants`
  drops by the size of the mesh blobs.
- T13 / M6 — `passthrough_missing_thumbnails` (215 lines, five phases)
  decomposed into `enumerate_target_entries`,
  `plan_thumbnail_passthrough`, `rewrite_archive_with_blobs`, and
  `atomic_swap_rewrite`. Each helper owns its `mz_zip_archive` lifetime.
- T14 / H3 — `rewrite_archive_with_blobs` carries forward entries via
  `mz_zip_writer_add_from_zip_reader` (raw deflated bytes, no
  decompress + recompress). Synthesized placeholder PNGs and source-
  copied blobs both use `MZ_NO_COMPRESSION` (PNG is already deflate-
  compressed). A fallback decompress+recompress path handles the rare
  miniz failure.
- T15 / H2 — `verify_vector_config_roundtrip` switches from
  `load_project(zip)` (LoadModel + LoadConfig — the most expensive
  operation in the pipeline) to a `load_bbs_3mf(..., LoadStrategy::
  LoadConfig)` config-only call. Mesh data is no longer re-parsed
  purely to diff vector-config keys.

Test count moved from 109 / 66046 (P7 baseline) to 123 / 66046+ assertions.
All e2e tests still pass; behavior of the CLI is unchanged except for the
following deliberate small differences:

- `inspect.cpp`'s missing-file error message is now uniform with the other
  subcommands ("input not found" instead of "file not found").
- JSON output key order may differ slightly (the `nlohmann::json` library
  preserves insertion order; the previous hand-rolled output had a fixed
  order). Field names and values are unchanged.

## P3 stride fix (2026-05-20)

Surfaced by the cumulative manual smoke above. `add_object`'s per-plate
world-offset stride was `bed_extent + 10.0`, but OrcaSlicer's
`PartPlateList::plate_stride_x` is `bed_extent * (1 + LOGICAL_PART_PLATE_GAP)`
with `LOGICAL_PART_PLATE_GAP = 1/5` (`src/slic3r/GUI/PartPlate.cpp:55`).
For a 256 mm bed that's 266 vs 307.2 — a 41 mm shortfall per plate,
visible as grid-placed objects landing in the inter-plate gutter when
multiple plates are present.

Pre-existing defect from v2 P3, NOT introduced by the cleanup pass.
Fixed in commit `01298884ce` with the stride replaced by the
proportional form, plus a `[gui-compat]` regression test pinning the
contract against OrcaSlicer's constant. Test count moved from 123 to
124 cases.
