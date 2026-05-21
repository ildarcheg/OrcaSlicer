# orca-cli: project-tab editing — design

**Date:** 2026-05-20
**Status:** approved (brainstorm complete; implementation plan pending)
**Predecessors:** v2 (`2026-05-19-orca-cli-design.md`), v4 merge-parts (`2026-05-20-orca-cli-merge-parts-design.md`).

## 1. Motivation and scope

OrcaSlicer's "Project" tab (`src/slic3r/GUI/Project.cpp::ProjectPanel`) is a
webview-rendered panel sourced from three structs on `Model`
(`src/libslic3r/Model.hpp:1475-1542`) plus an `AuxiliaryPanel` that manages
file attachments under `Metadata/Auxiliaries/`. Today none of those fields
is reachable from `orca-cli`.

The user's driving use case is to **stamp a 3mf with title, description,
hero image, profile details, and supporting documents (assembly guide,
pictures) from automation pipelines**, so the resulting 3mf opens in the
OrcaSlicer GUI with a fully populated Project tab.

**In scope** (this spec):
- ModelInfo fields visible on the Project tab: `title`, `description`,
  `license`, `copyright`, `cover` (hero image).
- ModelProfileInfo fields: `title`, `description`, `cover`.
- Auxiliary file attachments in four GUI-visible folders: `pictures`,
  `bom`, `assembly-guide`, `others`.

**Out of scope** (deferred, may become follow-up specs):
- ModelDesignInfo (`DesignId`, `Designer`) — Orca privacy policy strips
  `DesignerUserId` on save anyway; `Designer` is also derivable from
  `copyright` JSON which we already cover.
- MakerLab fields (`mk_name`, `mk_version`, `md_name[]`/`md_value[]`).
- Free-form `metadata_items` map (arbitrary key/value).
- Origin field (not in the user's named subset).
- Thumbnail manipulation (Thumbnail, Poster, Thumbnail_Small,
  Thumbnail_Middle — these are managed by the GUI's slicing pipeline and
  must not be touched casually).

## 2. Command surface

```
orca-cli project info show       <file>                                   # human / --json
orca-cli project info set        <file> [--title T] [--description D]
                                        [--license L] [--copyright C]
                                        [--cover IMG] [--output O]
orca-cli project info clear      <file> --field F[,F2,...]    [--output O]

orca-cli project profile show    <file>                                   # human / --json
orca-cli project profile set     <file> [--title T] [--description D]
                                        [--cover IMG] [--output O]
orca-cli project profile clear   <file> --field F[,F2,...]    [--output O]

orca-cli project aux list        <file>                                   # human / --json
orca-cli project aux add         <file> --folder pictures|bom|assembly-guide|others
                                        --file PATH [--name N] [--force] [--output O]
orca-cli project aux remove      <file> --folder F --name N   [--output O]
orca-cli project aux export      <file> --folder F --name N --to PATH
```

### 2.1 Semantics

- `info set` / `profile set` accept **multiple field flags per
  invocation**; one 3mf rewrite per call regardless of how many fields
  changed. At least one field flag must be supplied (zero → `BadInput`).
- `--cover IMG` is the only file-valued flag on `info`/`profile set`. It
  atomically (a) embeds the PNG/JPG at the canonical 3mf path
  `Auxiliaries/.thumbnails/thumbnail_3mf.png` (per `bbs_3mf.cpp:205`,
  `_3MF_COVER_FILE`) and (b) writes that pointer string into
  `model.model_info->cover_file` (for `info set`) or
  `model.profile_info->ProfileCover` (for `profile set`).
- Because the canonical path is shared, **`info` and `profile` cover
  images overwrite each other**. This matches the GUI's behaviour and is
  documented in each verb's `--help`.
- `info clear --field` and `profile clear --field` accept a comma-
  separated list. Each name is validated against the per-surface allowed
  set:
  - info: `title`, `description`, `license`, `copyright`, `cover`
  - profile: `title`, `description`, `cover`
  Unknown → `InvalidField` (exit 2) with an error message listing the
  legal names.
- Clearing `cover` additionally removes the embedded
  `Auxiliaries/.thumbnails/thumbnail_3mf.png` file from the auxiliary
  temp dir if present.
- Clearing an already-empty field is **idempotent** (exit 0, no-op).
- `--folder` (aux verbs) is an enum, not a free string. Mapping:
  - `pictures`       → `Auxiliaries/Model Pictures/`
  - `bom`            → `Auxiliaries/Bill of Materials/`
  - `assembly-guide` → `Auxiliaries/Assembly Guide/`
  - `others`         → `Auxiliaries/Others/`
  Folder names match the GUI buckets in `src/slic3r/GUI/Auxiliary.cpp:866-876`.
- `aux add --name N` overrides the in-3mf basename. Default = source
  basename. The name is sanitized: any `/`, `\`, or `..` in the name →
  `BadInput`.
- `aux add` collision (target name already exists in the bucket) →
  `FileExists` (exit 5) unless `--force` is supplied. `--force` over a
  byte-identical file is still exit 0.
- `aux export` is read-only — no model rewrite, no guard, no atomic
  rename.

### 2.2 Output formats

- `info show`, `profile show`, `aux list`: human-readable table by
  default. With global `--json` flag (inherited from v2), emit a flat
  JSON object whose keys match the GUI field names:
  ```
  info:    {"title","description","license","copyright","cover","origin"}
  profile: {"title","description","cover","user_id","user_name"}
  aux:     {"pictures":[…], "bom":[…], "assembly_guide":[…], "others":[…]}
  ```
  Each aux entry is `{"name","size"}`.
- `show` is the read-only window onto the whole struct: `info show`
  surfaces `origin` even though `info set` does not expose it; `profile
  show` surfaces `user_id` and `user_name` even though `profile set`
  does not expose them. These read-only fields fall under the
  out-of-scope follow-ups in § 7.
- If `model.model_info == nullptr` (pristine 3mf), `info show` emits an
  empty/null record — not an error.

## 3. Architecture

### 3.1 Locus

Two new files, mirroring the v2 / v4 layout:

- `src/cli/project_ops.cpp` — pure operations on an in-memory `Model`:
  `info_set`, `info_clear`, `profile_set`, `profile_clear`, `aux_add`,
  `aux_remove`, `aux_list`, `aux_export`, plus the helper
  `embed_cover_image`.
- `src/cli/commands/project.cpp` — thin parser/dispatcher for
  `project info|profile|aux …`. Wired into the top-level dispatcher in
  `src/cli/main.cpp` the same way `commands/object.cpp` and
  `commands/config.cpp` are.

### 3.2 Clone-and-mutate flow (unchanged from v2)

Every mutating verb:

1. `load_bbs_3mf(input)` → `Model` + `ProjectState`.
2. Mutate the in-memory `Model` (string fields on `model_info` /
   `profile_info`, or the auxiliary temp dir via
   `model.get_auxiliary_file_temp_path()`).
3. `store_bbs_3mf(tmp_output)` writes a new 3mf to a temp path.
4. v2 runtime guard runs on the temp output: `verify_relationships`,
   `verify_plate_thumbnails`, `verify_vector_config_roundtrip`.
5. Atomic rename → final path (or `--output` path). v2's
   restore-on-failure logic is unchanged.

**No new invariant** is added to the guard. These edits don't touch
geometry, plates, or PrintConfig — the existing guards are sufficient.

### 3.3 Aux file plumbing

OrcaSlicer already extracts auxiliaries to a temp directory on load
(`model.get_auxiliary_file_temp_path()`, used at
`src/slic3r/GUI/Project.cpp:156`). The CLI reuses that: mutate the temp
directory; `store_bbs_3mf` re-packs it on the way out. **No new 3mf-zip
code** — we leverage the existing pack/unpack pipeline.

### 3.4 Cover image embedding

`embed_cover_image(Model&, fs::path src, CoverTarget target)`:

1. Validate `src` exists and is readable; sniff the first 8 bytes; reject
   if not a PNG (`89 50 4E 47 0D 0A 1A 0A`) or JPG (`FF D8 FF`) →
   `BadCoverImage` (exit 2).
2. Copy bytes into the auxiliary temp dir at
   `.thumbnails/thumbnail_3mf.png` (canonical, regardless of source
   extension — matches `bbs_3mf.cpp:205`).
3. Set `model.model_info->cover_file` (if `target==Info`) or
   `model.profile_info->ProfileCover` (if `target==Profile`) to that
   canonical relative path.

If `target==Info` and `model.model_info == nullptr`, allocate it. Same
for `target==Profile` and `model.profile_info == nullptr`.

### 3.5 Read path

`show` verbs and `aux list` call `load_bbs_3mf` and emit. No mutation,
no guard, no rename. Cheap.

## 4. Data flow per verb

### `project info show <file>`
- Load → read `model.model_info` → emit human table or JSON.
- `model_info == nullptr` → emit empty/null record (exit 0).

### `project info set <file> [--title …] [--description …] [--cover IMG] …`
1. Parse flags; zero field-flags → `BadInput` (exit 2).
2. Load. Allocate `model_info` if nullptr.
3. For each supplied flag: assign to the matching `ModelInfo` string
   field, or call `embed_cover_image(.., CoverTarget::Info)` for `--cover`.
4. Store → guard → atomic rename.

### `project info clear <file> --field title,description,…`
1. Parse `--field` as comma list; validate each against
   `{title, description, license, copyright, cover}`. Unknown →
   `InvalidField` (exit 2).
2. Load.
3. For each named field: null it (empty string for strings). `cover`
   clear additionally deletes `Auxiliaries/.thumbnails/thumbnail_3mf.png`
   from the temp aux dir if present.
4. Store → guard → atomic rename. Idempotent.

### `project profile show / set / clear`
Symmetric to `info`, applied to `model.profile_info`:

- `ProfileTile` (sic — typo preserved from upstream) ↔ `title`
- `ProfileDescription` ↔ `description`
- `ProfileCover` ↔ `cover`

`profile set --cover` reuses `embed_cover_image(.., CoverTarget::Profile)`.
See § 2.1 on shared cover image semantics.

### `project aux list <file>`
- Load → walk the four bucket directories under
  `model.get_auxiliary_file_temp_path()` → emit table or JSON. No write.

### `project aux add <file> --folder F --file PATH [--name N] [--force]`
1. Validate `--folder` against the enum (`BadInput` exit 2 if unknown).
2. Stat `--file PATH`; missing/unreadable → `BadAuxFile` (exit 2).
3. Load.
4. Compute target name: `N` if given, else `basename(PATH)`. Sanitize:
   reject `/`, `\`, `..` → `BadInput`.
5. Collision check against the bucket subdir; present and not `--force` →
   `FileExists` (exit 5).
6. Copy source bytes into `<bucket>/<name>` in the temp aux dir.
7. Store → guard → atomic rename.

### `project aux remove <file> --folder F --name N`
1. Validate `--folder`.
2. Load.
3. `<bucket>/<name>` not present → `NotFound` (exit 7).
4. Delete file from temp aux dir.
5. Store → guard → atomic rename.

### `project aux export <file> --folder F --name N --to PATH`
1. Validate `--folder`; validate `--to PATH` parent dir is writable.
2. Load.
3. `<bucket>/<name>` not present → `NotFound` (exit 7).
4. Copy file from temp aux dir → `--to PATH`. **Overwrites without
   prompting** (same posture as v2's `--output O`: the user named the
   destination path explicitly, so the write is consent).

No model rewrite. No guard. No rename.

## 5. Error handling

### 5.1 Exit-code contract

Reused from v2 (`src/cli/error.hpp`, no code changes):

| Code | Class            | Used for |
|------|------------------|----------|
| 0    | (success)        | success and idempotent no-ops |
| 2    | `BadInput`       | missing input 3mf; malformed args; zero field-flags on `set`; unknown `--folder` enum; sanitization rejection on `--name` |
| 5    | `FileExists`     | aux collision without `--force` |
| 7    | `NotFound`       | `aux remove`/`aux export` for an absent name |

Added next to existing classes in `error.hpp`:

| Code | Class            | Used for |
|------|------------------|----------|
| 2    | `BadCoverImage`  | `--cover` path unreadable, missing, or magic-bytes don't match PNG/JPG. Error message includes offending path and detected prefix. |
| 2    | `BadAuxFile`     | `--file` for `aux add` unreadable or absent. Distinct from `BadCoverImage` so messages can be targeted. |
| 2    | `InvalidField`   | `clear --field X` where `X` isn't in the per-surface allowed set. Error message lists legal names for that surface. |

### 5.2 Idempotency rules

- `info clear --field title` when `title` is already empty → exit 0.
- `aux add --force` over a byte-identical file → exit 0.

### 5.3 Guard failures

Post-`store_bbs_3mf` rejection by any of `verify_relationships`,
`verify_plate_thumbnails`, `verify_vector_config_roundtrip` triggers v2's
restore-on-failure logic: original file preserved, exit 1 with the
guard's diagnostic. Not bypassed under any circumstance.

### 5.4 Deterministic validation order

Same shape as v4 § 3 deterministic precedence. Fail-fast:

1. CLI arg parsing (verb, subverb, required flags present).
2. Enum / whitelist checks (`--folder`, `--field`).
3. Source-file existence / readability (`--file`, `--cover`, `--to`
   parent dir).
4. Load the 3mf.
5. Domain checks (collision for `aux add`; presence for
   `aux remove`/`aux export`).
6. Mutate.
7. Store + guard + atomic rename.

Errors at steps 1-3 never touch the input 3mf. Errors at step 4 leave it
untouched. Errors at step 5+ leave it untouched because the rewrite
goes through a temp path that's only renamed on success.

### 5.5 Help text

Each new verb gets a `--help` block with one happy-path example
invocation. Same convention as `object add --help`.

## 6. Testing

Total new cases: **~32**. Suite grows from current ~155 to ~187.
Naming follows `tests/cli/{unit,e2e,roundtrip}/`.

### 6.1 Unit tests (~18 cases)

File: `tests/cli/unit/project_ops_test.cpp`

- `info_set`: sets all five fields; batches multiple flags;
  `model_info==nullptr` triggers allocation; idempotent re-set.
- `info_clear`: single field; multiple comma-separated; idempotent on
  already-empty; cover clear removes embedded image from temp aux dir.
- `profile_set` / `profile_clear`: symmetric coverage.
- `embed_cover_image`: PNG accepted; JPG accepted; non-image rejected
  with `BadCoverImage`; missing src rejected; both targets (Info,
  Profile) point at the same canonical path and overwrite each other.
- `aux_add`: happy path; `--name` rename; sanitize rejects path
  separators; collision without `--force` → `FileExists`; collision
  with `--force` → success; unknown folder enum → `BadInput`.
- `aux_remove`: happy path; missing name → `NotFound`; unknown folder
  → `BadInput`.
- `aux_list`: empty (returns four empty buckets, not error); populated
  bucket walk; JSON shape stable.
- `aux_export`: happy path; missing name → `NotFound`; unwritable `--to`
  parent → `BadInput`.

### 6.2 E2E tests (~12 cases)

File: `tests/cli/e2e/project_e2e_test.cpp`. Process-level invocations
of `orca-cli`, exit-code + stdout assertions.

- `info set` happy path → `info show --json` parses and round-trips.
- `info set` with zero field flags → exit 2.
- `info set --cover hero.png` → `info show --json` reports canonical
  cover path.
- `info clear --field title,description` → subsequent `info show --json`
  reports both empty.
- `profile set` + `profile show` parity case.
- `aux add --folder pictures --file X.png` → `aux list` reports it
  under pictures.
- `aux add` collision without `--force` → exit 5; with `--force` →
  exit 0.
- `aux remove` happy + missing-name (exit 7).
- `aux export --to PATH` → file byte-equal to original source.
- `--output O` honoured on each mutating verb (does not overwrite input).

### 6.3 Roundtrip tests (~2 cases)

File: `tests/cli/roundtrip/project_roundtrip_test.cpp`

- Reference 3mf → `info set` (all fields) → `profile set` (all fields)
  → `aux add` (one per folder) → save → reload → assert state.
  Verifies edits survive a full pack/unpack cycle and don't trip the v2
  runtime guard.
- Reference 3mf → `aux add` → `aux remove` → reload → assert file gone
  and bucket empty (regression for half-baked removal).

### 6.4 Manual smoke recipe

Added to `docs/cli/manual-test.md` as **Phase 10**:

```
1. Copy reference 3mf to TEMP.
2. orca-cli project info set    --title "Smoke" --description "..." --cover hero.png
3. orca-cli project profile set --title "Profile Smoke" --cover hero.png
4. orca-cli project aux add     --folder assembly-guide --file guide.pdf
5. orca-cli project aux add     --folder pictures       --file photo.png
6. Open in OrcaSlicer; Project tab should render:
   - model title/description/cover
   - profile title/cover
   - guide.pdf under Assembly Guide
   - photo.png under Pictures
```

This is the GUI gate that catches things tests miss; it caught two
production bugs in v2 per the project memory.

### 6.5 New fixtures

Three tiny files committed to `tests/cli/fixtures/`:

- `cover_smoke.png` (~200 bytes; valid PNG, 1×1 transparent)
- `cover_smoke.jpg` (~300 bytes; valid JPG, 1×1)
- `assembly_smoke.pdf` (~500 bytes; minimal valid PDF)

Total <2 KB added to the repo.

## 7. Out-of-scope follow-ups (future)

- `project meta set --key K --value V` for the long-tail
  `metadata_items` map and Origin field.
- `project designer set --designer NAME --design-id ID` for
  ModelDesignInfo.
- `project makerlab set --name N --version V` plus a way to manage
  `md_name[]`/`md_value[]` pairs.
- Pattern / wildcard support on `aux remove` (e.g. `--name "*.png"`).
- `aux add` accepting multiple `--file PATH` flags for bulk
  attachment in a single rewrite.
