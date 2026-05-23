# Orca-cli cross-project convergence log

**Branch:** `cross-project-convergence`
**Base:** `a7748fbd20` (main)
**Reports consulted:**
- `C:\Users\ildarcheg\AppData\Local\Temp\orca_slicer_changes_report.md`
- `C:\Users\ildarcheg\AppData\Local\Temp\bambu_studio_changes_report.md`

---

## Item 1 — Atomic .bak-swap save (verify first)

**Status:** SKIPPED — already present.

**Finding:** OrcaSlicer `src/cli/io.cpp::save_project` already implements the
.bak-swap pattern at `src/cli/io.cpp:467-499`. The sequence is:

1. `store_bbs_3mf` writes `<target>.tmp` (`io.cpp:439`).
2. `passthrough_missing_thumbnails` rewrites .tmp with thumbnail copy-through
   (`io.cpp:453`), itself doing a sub-swap target↔rewrite via
   `atomic_swap_rewrite` (`io.cpp:243-274`) which is the same .bak idiom.
3. `run_all_invariants(s, tmp_path)` runs against the .tmp BEFORE the final
   swap; on failure the .tmp is removed and the destination is untouched
   (`io.cpp:459-465`) — invariant-guard placement preserved.
4. **Final .bak swap** (`io.cpp:473-499`):
   - if dest exists: `rename(target → target.bak)`; on failure remove .tmp + throw
   - `rename(.tmp → target)`; on failure restore from .bak + throw
   - remove .bak (best-effort)

This is the same pattern Bambu's report at `src/cli/io.cpp:284-310` claims
was "ported from OrcaSlicer M11" — Orca is the **source**, Bambu the
destination. The Orca report's "atomic rename" phrasing was loose; the
implementation is .bak-swap.

**Tested:** N/A — no code change.

**Commit:** None (nothing to land).

NOTE: same outcome as Item 1 — convergence-already-done. Items 1 & 2 should
likely be removed from the next convergence pass, or reformulated as
sibling-side ports (Orca → Bambu) rather than the reverse.

---

## Item 2 — plate_world_origin formula (BBS PartPlateList stride)

**Status:** SKIPPED — porting Bambu's signature would REGRESS Orca vs. the GUI.

**Finding (three-way reconcile against the GUI source-of-truth):**

GUI authoritative path (`src/slic3r/GUI/PartPlate.cpp`, `PartPlate.hpp`):
- `LOGICAL_PART_PLATE_GAP = 1.0 / 5.0` → stride = bed × 1.2 (`PartPlate.cpp:55`).
- Origin: `col * stride_x, -row * stride_y` (`PartPlate.cpp:3842-3843`).
- **Column count comes from TOTAL plate count**:
  `m_plate_cols = compute_colum_count(m_plate_count)` (`PartPlate.cpp:4776`).
- `compute_colum_count(n)` is mathematically equivalent to `ceil(sqrt(n))` for
  any positive integer n (`PartPlate.hpp:38-50`; proven below).

Orca CLI today (`src/cli/placement.cpp:10-27`, called from
`src/cli/project_ops.cpp:176-180`):
- Stride: `(bed.max - bed.min) * (1 + 1.0/5.0)` — bed × 1.2 ✓
- Cols: `ceil(sqrt(total_plates))` ≡ GUI's `compute_colum_count(m_plate_count)` ✓
- Origin: `col * stride_x, -row * stride_y` ✓

Bambu CLI (`src/cli/project_ops.cpp:141-164`):
- Stride: `bed * (1 + 0.2)` — bed × 1.2 ✓
- Cols: `compute_colum_count(plate_index_1based)` — uses the **target plate
  index**, NOT the total plate count ✗ vs. GUI.

**Numerical proof of the GUI divergence in Bambu's formula** — placing object
on plate 4 of a 6-plate project, stride s:

| variant | cols | (col,row) of plate 4 (0-based idx=3) | world offset |
|---------|------|--------------------------------------|--------------|
| GUI / Orca CLI | `compute_colum_count(6)=3` | (0, 1) | `(0, -s)` |
| Bambu CLI      | `compute_colum_count(4)=2` | (1, 1) | `(s, -s)` |

So Bambu CLI would land the object one stride to the right of where the GUI
draws plate 4. Orca CLI already lands it on top of where the GUI draws it.

**Equivalence proof for `compute_colum_count(n) ≡ ceil(sqrt(n))`** (over
positive integers):
- If `sqrt(n) ∈ {integer p}`: ceil = p; cct returns rv=p, no bump → p.
- If `sqrt(n) ∈ (p, p+0.5)`: ceil = p+1; cct: rv = round = p, v>rv so +1 → p+1.
- If `sqrt(n) ∈ (p+0.5, p+1)`: ceil = p+1; cct: rv = round = p+1, v<rv → p+1.
- `sqrt(n) = p+0.5` exactly is impossible for integer n (would need
  `4n = (2p+1)² = odd`; but 4n is always even).

**Why the user's premise needs flipping for this item:** The convergence
prompt asked me to "Replace Orca's sqrt-grid cross-plate offset … with the
BBS PartPlateList stride formula … This is what the GUI uses." But the
"BBS PartPlateList stride formula" reference is the GUI code at
`PartPlate.cpp:3842-3843` + `:4776`, not Bambu's CLI re-implementation —
and the GUI version is exactly what Orca's CLI already does. The Bambu CLI
function (`plate_world_origin`) is the outlier; it's the one that would
need fixing if convergence is to mean "both CLIs match the GUI."

**Decision:** Per the user's standing instruction ("If any item breaks
tests, stop and document — do not paper over"), the AGENTS.md rule
("Changes must not cause regressions … defaults"), and the user's own
framing ("This is what the GUI uses, so CLI placement will match what
users see"), Orca is left unchanged. The right cross-project move is the
inverse — Bambu CLI should be updated to use total plate count — but that
is out of scope for this branch (Bambu repo is read-only here).

**Tested:** N/A — no code change.

**Commit:** None (nothing to land).

---

## Item 3 — Link-time stubs for Http + BBL_Encrypt

**Status:** SKIPPED — symbols never reach Orca's link line. Empirically and
structurally confirmed.

**Structural argument:**

Orca's `orca_cli_core` (`src/cli/CMakeLists.txt:32-38`) links exactly:
- `libslic3r` (the engine, no GUI / no network code)
- `Boost::filesystem`, `Boost::system`, `Boost::thread`
- `nlohmann_json`

Critically, `orca_cli_core` does **not** link `libslic3r_gui`. The
`Slic3r::Http` class lives in `src/slic3r/Utils/Http.cpp` (GUI tree only;
confirmed via `git ls-files src/slic3r/Utils/Http.{cpp,hpp}`). The
`BBL_Encrypt` class lives in BambuStudio's `src/slic3r/Utils/BBLUtil.{cpp,hpp}`;
**OrcaSlicer has no such file** (the `BBLUtil.*` pair is Bambu-only —
`git ls-files src/slic3r/Utils/BBLUtil.*` returns nothing on the Orca tree).

A grep against the entire `src/libslic3r/` tree for
`#include.*[Hh]ttp | BBL_Encrypt | Slic3r::Http` returns NO files
(verified). So libslic3r does not pull these symbols by reference, and the
static archive linker has nothing to resolve from the GUI tree.

**Empirical confirmation (MSVC link line):**

Inspected `build/src/cli/orca-cli.vcxproj` `<AdditionalDependencies>`
(line 131). The full link list:

- `orca_cli_core.lib`, `libslic3r.lib`, `libslic3r_cgal.lib`, `libnest2d.lib`
- `miniz_static.lib`, `opencv_world460.lib`, `ipp*`, `libpng*`, `zlib.lib`,
  `expat.lib`, `OCCT TK*.lib` (CAD), `clipper*.lib`, `draco.lib`,
  `glu-libtess.lib`, `jpeg-static.lib`, `admesh.lib`
- `boost_{log,log_setup,coroutine,context,serialization,locale,
  program_options,nowide,iostreams,random,filesystem,thread,atomic,
  chrono,date_time,container,exception}-vc144-mt-x64-1_84.lib`
- `libmpfr-4.lib`, `libgmp-10.lib`, `mcutd.lib`, `libnoise_static.lib`,
  `qhullstatic.lib`, `qoi.lib`, `semver.lib`, `tbbmalloc.lib`, `tbb12.lib`,
  `nlopt.lib`, `libopenvdb.lib`, `Half-2_5.lib`, `libblosc.lib`
- `libcrypto.lib` (OpenSSL **Crypto**, used by libslic3r for MD5/AES hashing
  — NOT OpenSSL SSL, NOT a network dep)
- Windows system libs: `opengl32.lib`, `windowscodecs.lib`, `winmm.lib`,
  `freetype.lib`, `gdi32.lib`, `advapi32.lib`, `user32.lib`, `wsock32.lib`,
  `psapi.lib`, `secur32.lib`, `ws2_32.lib`, `mswsock.lib`, `kernel32.lib`,
  `winspool.lib`, `shell32.lib`, `ole32.lib`, `oleaut32.lib`, `uuid.lib`,
  `comdlg32.lib`, `bcrypt.lib`, `synchronization.lib`, `Psapi.lib`

**Verified absent (user's three callouts):**
- `libcurl` / `curl.lib` → not in the link line.
- `libssl` (OpenSSL **SSL**) → not in the link line.
- `crypt32.lib` → not in the link line.

(Note: `bcrypt.lib` ≠ `crypt32.lib`. The former is Windows' modern crypto
shim pulled by boost::uuid's random generator; entirely inert and standard
for any boost-using Windows binary.)

**Why Bambu needs stubs but Orca doesn't:** BambuStudio's libslic3r tree
apparently references `Slic3r::Http` and `BBL_Encrypt` somewhere — the
`stubs_for_libslic3r.cpp` header comment explicitly says "Do NOT compile
src/slic3r/Utils/Http.cpp or src/slic3r/Utils/BBLUtil.cpp into bambu-cli —
these stubs replace them." That implies `bambu_cli_core`'s sources or
`libslic3r`'s sources transitively include those `.cpp` files. Orca's
build does not have that issue.

**Tested:** Inspected vcxproj link line for both Debug and Release configs —
both clean (verified the Debug section at line 131; Release section
verified by the same Grep pattern showing only the include-dirs differ).

**Commit:** None (nothing to land).

---

## Item 4 — Combined pre-save template check (short-path tolerance + TOCTOU)

**Status:** LANDED.

**Finding:** Orca's `verify_input_template_thumbnails`
(`src/cli/invariants.cpp:290`) was already operating on the staging copy
(the `.init-tmp` file produced by `do_project_init`, see
`commands/project_init.cpp:40-55`), so the **TOCTOU defense was already in
place**. The missing piece was the **short-path open**: the function went
through `enumerate_zip_entry_names` → `Slic3r::open_zip_reader` →
`boost::nowide::fopen` (`src/libslic3r/miniz_extension.cpp:18-25`), which
on Windows converts UTF-8 → UTF-16 → `_wfopen`. That round-trip can fail
on 8.3 short-form path components (e.g. `C:\Users\ILDARC~1\AppData\Local\
Temp\...`) on some toolchains.

**Change:** `src/cli/invariants.cpp:290-330` rewrites
`verify_input_template_thumbnails` to open the archive directly via
`mz_zip_reader_init_file` and enumerate entries inline. Same exception
contract (open failure → "cannot open input template"; thumbnail miss →
"regenerate in OrcaSlicer GUI"). No header additions needed —
`<libslic3r/miniz_extension.hpp>` was already included at line 5.

**Sibling-parity ref:** BambuStudio's
`src/cli/invariant_guard.cpp::check_thumbnails_in_archive` (lines 160-182,
with the explicit comment about 8.3 shortname paths in TEMP at line 165-166).

**Tested:**
- `msbuild build\src\cli\orca_cli_core.vcxproj /m /p:Configuration=Release`
  → builds clean.
- `msbuild build\src\cli\orca-cli.vcxproj /m /p:Configuration=Release`
  → builds clean (pre-existing LIBCMT/LNK4098 warning from tbbmalloc is
  unrelated; ditto the pre-existing tbbmalloc /LTCG restart).
- `msbuild build\tests\cli\cli_tests.vcxproj /m /p:Configuration=Release`
  → builds clean (same pre-existing warnings).
- `cli_tests.exe --order rand --warn NoAssertions --reporter JUnit` →
  **tests=66871 failures=0 errors=0 skipped=0, exit code 0**.
- Critical regression coverage: `test_project_init.cpp:43`
  ("project init exits 8 on input template with missing plate thumbnail")
  exercises the new primitive — it uses `make_temp_dir()` →
  `fs::temp_directory_path()` which on this Windows config can return
  the 8.3 short form. Test passed under the new code path.

**Commit:** `67c5848` — `fix(cli): open input template via
mz_zip_reader_init_file (short-path tolerance)`.

---

## Branch & log paths

- Branch: `cross-project-convergence`
- Log: `C:\Users\ildarcheg\AppData\Local\Temp\orca_convergence_log.md`
- Item 4 only: one commit on top of `main` (`a7748fbd20`). Items 1-3
  were no-ops (already converged or never divergent).

