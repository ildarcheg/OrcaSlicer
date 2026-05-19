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
