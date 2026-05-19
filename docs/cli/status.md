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
