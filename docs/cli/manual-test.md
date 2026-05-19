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
