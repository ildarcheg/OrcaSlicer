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

## Phase 4 - object transforms

```powershell
$OUT = "$env:TEMP\orca-cli-p4.3mf"
Copy-Item $REF $OUT -Force
& $CLI plate  add $OUT --name T
& $CLI object add $OUT --plate T --stl "$STLS\000_01_test_cube.stl" --translate 60,60 --name plain
& $CLI object add $OUT --plate T --stl "$STLS\000_01_test_cube.stl" --translate 120,60 --scale 2 --name big
& $CLI object add $OUT --plate T --stl "$STLS\000_01_test_cube.stl" --translate 90,120 --rotate 0,0,0.7854 --name spun
```

Expected: 3 cubes on plate T at the specified plate-local positions --
`plain` at (60,60), `big` (twice the size, --scale 2) at (120,60), `spun`
(rotated 45 deg about Z, pi/4 radians) at (90,120). All visible in
OrcaSlicer's renderer. When any transform flag is supplied, `--count N`
stacks N copies at the same post-transform position instead of laying
them out on the per-plate grid (the P3 behavior).

Anti-cases (each should exit non-zero with a clean message):

```powershell
& $CLI object add $OUT --plate T --stl "$STLS\000_01_test_cube.stl" --translate 99999,99999
# expected: exit 9 (placement_failure)

& $CLI object add $OUT --plate T --stl "$STLS\000_01_test_cube.stl" --translate "not,a,number"
# expected: exit 1 (usage_error)

& $CLI object add $OUT --plate T --stl "$STLS\000_01_test_cube.stl" --rotate 0,0
# expected: exit 1 (usage_error - --rotate requires 3 components)
```

## Phase 5 - object filaments

The reference 3mf has 6 filament slots (Bambu PLA Basic @BBL A1 x6), so
`--filament 1..6` are valid; `--filament 7+` are out of range.

```powershell
$OUT = "$env:TEMP\orca-cli-p5.3mf"
Copy-Item $REF $OUT -Force
& $CLI plate  add $OUT --name F
& $CLI object add $OUT --plate F --stl "$STLS\000_01_test_cube.stl" --name cube1 --filament 1
& $CLI object add $OUT --plate F --stl "$STLS\000_01_test_cube.stl" --name cube2
& $CLI object set-filament $OUT --name cube2 --filament 2
```

Expected: in OrcaSlicer's object panel, `cube1` is assigned to filament
slot 1 and `cube2` to slot 2; both render normally on plate `F`. Bug C
defense -- source attribution holds even when `--filament` writes
per-object extruder, so neither cube is silently dropped on GUI open.

Anti-cases (each should exit non-zero with a clean message):

```powershell
& $CLI object add $OUT --plate F --stl "$STLS\000_01_test_cube.stl" --filament 99 --name bad
# expected: exit 6 (unknown_reference - filament slot out of range)

& $CLI object set-filament $OUT --name cube1 --filament 0
# expected: exit 6 (unknown_reference - slot < 1 also out of range)

& $CLI object set-filament $OUT --name ghost --filament 1
# expected: exit 6 (unknown_reference - object name not found)
```

## Phase 6 - config

```powershell
$OUT = "$env:TEMP\orca-cli-p6.3mf"
Copy-Item $REF $OUT -Force
& $CLI plate  add  $OUT --name C
& $CLI object add  $OUT --plate C --stl "$STLS\000_01_test_cube.stl" --name cubeC
& $CLI config set  $OUT --object cubeC --key wall_loops --value 4
& $CLI config set  $OUT --key sparse_infill_density --value 30%
& $CLI config list $OUT --object cubeC --changed-only
& $CLI config list $OUT --changed-only
```

Expected: in OrcaSlicer's per-object settings pane, `cubeC` shows
`wall_loops = 4`; in the global process-settings pane,
`sparse_infill_density = 30%`. Both `config list` commands print
non-empty key/value output (one `<key> = <value>` line per key). The
project-level `--changed-only` listing surfaces hundreds of keys
because the reference 3mf carries a fully-populated Bambu A1 profile;
the per-object listing surfaces just the explicit `wall_loops` we
set.

Anti-cases (each should exit non-zero with a clean message):

```powershell
& $CLI config set   $OUT --key no_such_key --value 1
# expected: exit 4 (bad_config - key not in print_config_def)

& $CLI config set   $OUT --key layer_height --value not-a-num
# expected: exit 4 (bad_config - value rejected by set_deserialize)

& $CLI config set   $OUT --object ghost --key wall_loops --value 2
# expected: exit 6 (unknown_reference - object name not found)

& $CLI config unset $OUT --key no_such_key
# expected: exit 4 (bad_config - key not in print_config_def)

& $CLI config list  $OUT --output anywhere.3mf
# expected: exit 1 (usage_error - --output not allowed on list)
```

## Phase 7 - `inspect` + cumulative happy-path recipe

Read-only diagnostic dump. Combines every prior phase into a single
end-to-end exercise: clone the reference, add plates, add objects with
transforms and filaments, override project + per-object config, then
verify the result with `inspect`.

```powershell
$REF  = "C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates\temp_project_for_orca_slicer.3mf"
$STLS = "C:\Users\ildarcheg\Documents\GitHub\slicer_tamplates"
$OUT  = "$env:TEMP\orca-cli-p7-full.3mf"
$CLI  = "$pwd\build\src\cli\Release\orca-cli.exe"

Remove-Item $OUT -Force -ErrorAction SilentlyContinue
& $CLI project init $OUT --template $REF
& $CLI plate add    $OUT --name "Brackets"
& $CLI plate add    $OUT --name "Spares"
& $CLI plate rename $OUT --from "Spares" --to "Storage"
& $CLI object add   $OUT --plate "Brackets" --stl "$STLS\000_01_test_cylinder.stl" --name cyl  --filament 1
& $CLI object add   $OUT --plate "Brackets" --stl "$STLS\000_01_test_cone.stl"     --count 3   --name cone --filament 2
& $CLI object add   $OUT --plate "Storage"  --stl "$STLS\000_01_test_cube.stl"     --translate 60,60 --scale 2 --name big
& $CLI config set   $OUT --object cyl --key wall_loops --value 4
& $CLI config set   $OUT --key sparse_infill_density --value 30%
& $CLI object add $OUT --plate "Brackets" --stl "$STLS\box_with_text.stl" --name multi
& $CLI object split-to-parts $OUT --name multi
& $CLI object set-filament $OUT --name multi --part multi_1 --filament 1
& $CLI inspect      $OUT
& $CLI --json inspect $OUT     # structured form; pipe through ConvertFrom-Json for browsing
```

Expected: in OrcaSlicer:

- Plates 1, 2 come through from the reference; plates 3 (`Brackets`)
  and 4 (`Storage`) are newly added.
- `Brackets` has 1 `cyl` (filament slot 1, wall_loops = 4 per-object
  override) plus 3 `cone` instances (filament slot 2).
- `Storage` has 1 `big` cube, twice the size (--scale 2), translated
  to plate-local (60,60).
- Process panel shows `sparse_infill_density = 30%`.
- Every object renders cleanly, no crash on load.

Expected from `inspect`:

- Human mode lists `plates: 4`, `filament slots: 6`, then per-plate
  blocks naming the contained ModelObjects, then the project's
  changed config keys (hundreds, including `sparse_infill_density`),
  then per-object key lists (`cyl` carries `wall_loops`; `big`
  carries `extruder` / `wall_loops` only if explicitly set).
- JSON mode emits `{"plate_count":4, "filament_count":6,
  "plates":[...], "project_changed":[...], "objects":[...]}` inside
  the `data` object.

Anti-cases (each should exit non-zero with a clean message):

```powershell
& $CLI inspect $OUT --output anywhere.3mf
# expected: exit 1 (usage_error - --output not allowed on inspect)

& $CLI inspect "C:\does\not\exist.3mf"
# expected: exit 2 (file_not_found)
```

## Phase 8 - `object split-to-parts` + per-part filament

Requires `box_with_text.stl` in `$STLS` (copy from
`C:\Users\ildarcheg\Documents\GitHub\` once per session).

```powershell
$OUT = "$env:TEMP\orca-cli-p8.3mf"
Copy-Item $REF $OUT -Force
& $CLI plate add $OUT --name "MultiPlate"
& $CLI object add $OUT --plate "MultiPlate" --stl "$STLS\box_with_text.stl" --name multipart
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

& $CLI object set-filament $OUT --name multipart --part nope --filament 1
# expected: exit 6 (unknown_reference)

& $CLI object split-to-parts $OUT --name "__missing__"
# expected: exit 6 (unknown_reference)
```

Manual GUI smoke: open `$OUT` in OrcaSlicer; the `multipart` object
should appear as a single object with N parts visible in the object
panel, each part assigned to its respective filament slot.

