# Building orca-cli

This doc covers the orca-cli binary and the cli_tests harness on Windows.
For full-build instructions for OrcaSlicer itself, see `AGENTS.md` and the
top-level `BuildLinux.sh` / `build_release_vs2022.bat`.

## Toolchain

Visual Studio 2022 (v143), Windows SDK 10.0.22621.0 or newer, CMake 3.25+.
Per project memory, `pwsh` is NOT available — use `powershell.exe` (the
full Windows PowerShell 5.1 binary).

## Known issue: cmake configure is broken

Running `cmake -S . -B build` against a fresh checkout currently fails with:

```
CMake Error at tests/catch2/src/CMakeLists.txt:370 (target_compile_features):
  target_compile_features may only be set if policy CMP0067 is set to NEW...
```

`tests/catch2/src/` is the vendored Catch2 v3.11.0 source tree. Modifying it
would diverge on future Catch2 upgrades, so we do NOT patch the file in tree.

## Workaround: drive msbuild against the existing .vcxproj

The `build/` directory under source control was generated once with a CMake
version that handled the Catch2 file cleanly, and the resulting `.vcxproj`
files are still valid. Build orca-cli and cli_tests directly:

```powershell
$msbuild = (Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\Community\Msbuild\Current\Bin\MSBuild.exe').FullName

# orca-cli binary
& $msbuild "build\src\cli\orca-cli.vcxproj" /m /p:Configuration=Release /v:q

# cli_tests harness
& $msbuild "build\tests\cli\cli_tests.vcxproj" /m /p:Configuration=Release /v:q
```

Outputs:
- `build\src\cli\Release\orca-cli.exe`
- `build\tests\cli\Release\cli_tests.exe`

## Adding a new source or test file

The CMakeLists.txt globs cover new files, but because `cmake -S . -B build`
fails, the globs don't re-run. You must hand-add the new file to the
`.vcxproj` until the Catch2 configure issue is resolved.

1. Open `build\src\cli\orca-cli.vcxproj` (or `build\tests\cli\cli_tests.vcxproj`).
2. Find the `<ItemGroup>` block containing the existing `<ClCompile Include="..." />` lines.
3. Add a new `<ClCompile Include="..\..\..\path\to\new_file.cpp" />` entry.
4. Save and re-run the matching `msbuild` invocation above.

## Running tests

```powershell
# Random order + zero-assertion warning is the project convention.
build\tests\cli\Release\cli_tests.exe --order rand --warn NoAssertions

# JUnit reporter for CI / diffing across runs.
build\tests\cli\Release\cli_tests.exe --order rand --warn NoAssertions `
    --reporter "JUnit::out=junit.xml"

# Filter by tag.
build\tests\cli\Release\cli_tests.exe "[orca-cli][P3]" --order rand --warn NoAssertions
```

## When to fix the configure error instead

If you are upgrading Catch2 or making a change that needs `cmake -S . -B build`
to re-run cleanly, fix the `target_compile_features` invocation in the bundled
Catch2 (a `cmake_policy(SET CMP0067 NEW)` before the affected line is the
likely fix, but verify against the Catch2 release matching `tests/catch2/`).
Do not change project-level toolchain settings to work around the bundled
package — the issue is local to `tests/catch2/src/CMakeLists.txt`.
