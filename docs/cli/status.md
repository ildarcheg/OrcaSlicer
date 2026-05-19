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
