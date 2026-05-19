# orca-cli - manual GUI smoke recipe

Each phase appends a section. Always start by re-running every prior section, then the new one. Reference fixture is **never modified in place**; every recipe copies it to `$env:TEMP` first.

## Setup (run once per session)

```powershell
$REF  = "C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\temp_project_for_orca_slicer.3mf"
$STLS = "C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates"
$CLI  = "$pwd\build\src\cli\Release\orca-cli.exe"
```

## Phase 0 - Sanity

```powershell
& $CLI --version
```

Expected: prints `orca-cli 0.1.0-dev` and exit code 0.

## Phase 1 - `project init`

```powershell
$OUT = "$env:TEMP\orca-cli-p1.3mf"
Remove-Item $OUT -Force -ErrorAction SilentlyContinue
& $CLI project init $OUT --template $REF
& $CLI --json project init $OUT --template $REF   # idempotent re-run; exit 0
```

Expected: both invocations exit 0. The first prints `ok: initialized
project at ...`; the second prints a single JSON line
`{"status":"ok","code":"ok","message":"..."}`. After both runs, open
`$OUT` in OrcaSlicer and verify:

- the plate count and object set match the reference 3mf;
- plate thumbnails render in the plate panel;
- objects show in the scene with correct names;
- the printable area matches the source bed.

Anti-cases (each should exit non-zero with a clean message):

```powershell
& $CLI project init "$env:TEMP\noop.3mf" --template "C:\does\not\exist.3mf"
# expected: exit 2 (file_not_found), prints "err: file_not_found: ..."

& $CLI project init    # no positional arg or --template
# expected: exit 109 (CLI11 parse error), prints CLI11 usage to stderr
```

## Phase 2 - plate ops

```powershell
$OUT = "$env:TEMP\orca-cli-p2.3mf"
Copy-Item $REF $OUT -Force
& $CLI plate add    $OUT --name Brackets
& $CLI plate add    $OUT --name Auxiliary
& $CLI plate rename $OUT --from Auxiliary --to Spares
& $CLI plate list   $OUT
& $CLI plate add    $OUT --name Sidecar --output "$env:TEMP\orca-cli-p2-side.3mf"
```

Expected: `$OUT` has plates `Brackets` and `Spares` (renamed); the side-car file `...-p2-side.3mf` additionally has `Sidecar`. Both files open in OrcaSlicer with all plates visible and named correctly in the plate switcher. New plates show the 128x128 gray placeholder thumbnail in the plate panel.

Anti-cases (each should exit non-zero with a clean message):

```powershell
& $CLI plate add    $OUT --name Brackets         # duplicate
# expected: exit 5 (duplicate_name)

& $CLI plate rename $OUT --from missing --to X   # unknown from
# expected: exit 6 (unknown_reference)

& $CLI plate list   $OUT --output anywhere.3mf   # --output not allowed on list
# expected: exit 1 (usage_error)
```

## Phase 3 - object add (auto-place)

```powershell
$OUT = "$env:TEMP\orca-cli-p3.3mf"
Copy-Item $REF $OUT -Force
& $CLI plate  add $OUT --name Brackets
& $CLI object add $OUT --plate Brackets --stl "$STLS\000_01_test_cylinder.stl" --name cyl
& $CLI object add $OUT --plate Brackets --stl "$STLS\000_01_test_cone.stl" --count 3 --name cone
& $CLI object list $OUT
```

Expected: 2 new ModelObjects on the `Brackets` plate -- `cyl` (1 instance)
and `cone` (3 instances). All 4 instances render in OrcaSlicer (Bug C
defended -- every ModelVolume has `source.input_file` stamped to the STL
path so the GUI does not silently drop the part on load).

Note: `orca-cli object list` and `orca-cli plate list` currently display
0 objects per plate even on the unmodified reference 3mf. That is a
pre-existing load_project / `loaded_id` rebuild gap (the saved
`objects_and_instances` are correct -- the GUI loads them fine -- but
the CLI's in-memory reconstruction misses them). Use OrcaSlicer's plate
panel as the ground truth for plate membership until the rebuild is
hardened in a later phase.

Anti-cases (each should exit non-zero with a clean message):

```powershell
& $CLI object add $OUT --plate Nope --stl "$STLS\000_01_test_cube.stl"
# expected: exit 6 (unknown_reference)

& $CLI object add $OUT --plate Brackets --stl "C:\does\not\exist.stl"
# expected: exit 2 (file_not_found)

& $CLI object list $OUT --output anywhere.3mf
# expected: exit 1 (usage_error)

& $CLI object remove $OUT --name ghost-does-not-exist
# expected: exit 6 (unknown_reference)
```
