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
