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
- ModelInfo fields (`Model.hpp:1494-1516`) visible on the Project tab,
  editable via `info set`/`info clear`: `title`, `description`,
  `license`, `copyright`, `cover` (hero image).
- ModelProfileInfo fields (`Model.hpp:1475-1483`), editable via
  `profile set`/`profile clear`: `title`, `description`, `cover`. The
  struct also holds `ProfileUserId` and `ProfileUserName` (Bambu
  account identifiers); these are exposed read-only via `profile show`
  but not editable here — see § 7.
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
  changed. At least one field flag must be supplied (zero → CLI11
  rejects → `usage_error` exit 1).
- `--cover IMG` is the only file-valued flag on `info`/`profile set`.
  **PNG-only** — the canonical embed path in the 3mf is fixed at
  `Auxiliaries/.thumbnails/thumbnail_3mf.png` (`bbs_3mf.cpp:205`), and
  the exporter wires the OPC cover-thumbnail relationship by exact
  string match on that path (`bbs_3mf.cpp:8513`). Storing JPG bytes
  under the `.png` filename would break that match and lose the cover
  wiring on save, so we refuse JPG at parse time rather than silently
  mis-wire. Users with a JPG convert it themselves.
  - On success the verb atomically (a) copies the PNG bytes to the
    canonical path and (b) writes that path into
    `model.model_info->cover_file` (`info set`) or
    `model.profile_info->ProfileCover` (`profile set`).
- Because the canonical path is shared, **`info` and `profile` cover
  images overwrite each other on `set`** (last writer wins). This
  matches the GUI's behaviour and is documented in each verb's
  `--help`.
- `info clear --field` and `profile clear --field` accept a comma-
  separated list. Each name is validated against the per-surface allowed
  set:
  - info: `title`, `description`, `license`, `copyright`, `cover`
  - profile: `title`, `description`, `cover`
  Unknown → `InvalidField` (exit 4 `bad_config`) with an error message
  listing the legal names.
- **`clear --field cover` is a refcount-style operation** on the shared
  image. The verb nulls the pointer on the named surface only
  (`model_info->cover_file` or `profile_info->ProfileCover`). The
  embedded file at `Auxiliaries/.thumbnails/thumbnail_3mf.png` is
  deleted from the auxiliary temp dir **only when both pointers are
  empty after the clear**. This prevents the surface-A-clear-deletes-
  surface-B-image footgun. Concretely:
  `info set --cover A.png; profile set --cover B.png; info clear
  --field cover` leaves `profile_info->ProfileCover` pointing at the
  still-present `thumbnail_3mf.png` (which now holds B's bytes from the
  second `set`); a subsequent `profile clear --field cover` finally
  removes the embedded file.
- Clearing an already-empty field is **idempotent** (exit 0, no-op).
  Idempotent clear on `cover` follows the same refcount rule: if both
  pointers were already empty, no file delete is attempted.
- `--folder` (aux verbs) is an enum, not a free string. Mapping:
  - `pictures`       → `Auxiliaries/Model Pictures/`
  - `bom`            → `Auxiliaries/Bill of Materials/`
  - `assembly-guide` → `Auxiliaries/Assembly Guide/`
  - `others`         → `Auxiliaries/Others/`
  Folder names match the GUI buckets in `src/slic3r/GUI/Auxiliary.cpp:866-876`.
  Note the JSON shape (§ 2.2) uses `assembly_guide` (underscore) for
  the same bucket — see § 2.3 for the naming convention.
- `aux add --name N` overrides the in-3mf basename. Default = source
  basename. The name is sanitized; any of the following → `AuxNameError`
  (exit 4 `bad_config`):
  - empty string
  - any `/`, `\`, or null byte (`\0`)
  - any path component equal to `.` or `..`
  - leading or trailing whitespace, leading or trailing `.`
  - Windows reserved device names (case-insensitive, with or without
    extension): `CON`, `PRN`, `AUX`, `NUL`, `COM1`-`COM9`, `LPT1`-`LPT9`
  These break silently on Windows extraction (most slicer users are on
  Windows), so the CLI rejects them up-front rather than producing a
  3mf that fails to round-trip.
- `aux add` collision (target name already exists in the bucket) →
  `AuxCollisionError` (exit 5 `duplicate_name`) unless `--force` is supplied. `--force` over a
  byte-identical file is still exit 0.
- `aux export` is read-only — no model rewrite, no guard, no atomic
  rename. If `--to PATH` resolves to an existing directory, the file is
  written to `PATH/<name>` (where `<name>` is the in-3mf basename);
  otherwise `PATH` is treated as the destination file. Either way,
  existing destination files are overwritten without prompting (the
  user named the path explicitly — see § 4 for full data flow).

### 2.2 Output formats

- `info show`, `profile show`, `aux list`: human-readable table by
  default. With global `--json` flag (inherited from v2), emit a flat
  JSON object whose keys match the field names. Concrete shapes:
  ```
  info:    {"title","description","license","copyright","cover","origin"}
           — all strings; reads from `model.model_info`
           (`Model.hpp:1494-1516`).
  profile: {"title","description","cover","user_id","user_name"}
           — all strings; reads from `model.profile_info`
           (`Model.hpp:1475-1483`). `title` ← `ProfileTile` (sic),
           `description` ← `ProfileDescription`, `cover` ← `ProfileCover`,
           `user_id` ← `ProfileUserId`, `user_name` ← `ProfileUserName`.
  aux:     {"pictures":[…], "bom":[…], "assembly_guide":[…], "others":[…]}
           — each entry is `{"name":string, "size":int}` (size in bytes).
  ```
- `show` is the read-only window onto the whole struct: `info show`
  surfaces `origin` even though `info set` does not expose it; `profile
  show` surfaces `user_id` and `user_name` even though `profile set`
  does not expose them. These read-only fields fall under the
  out-of-scope follow-ups in § 7.
- **Stable shape on pristine / missing structs.** When `model.model_info
  == nullptr` (pristine 3mf), `info show --json` still emits the full
  key set with empty-string values:
  ```json
  {"title":"","description":"","license":"","copyright":"","cover":"","origin":""}
  ```
  Same for `profile show --json` when `model.profile_info == nullptr`.
  Same for `aux list --json` when no bucket has files (each bucket key
  is present with an empty array `[]`). Consumers can always assume the
  full key schema; they never need to distinguish missing-struct from
  all-fields-cleared. Exit code is `0` in all cases.
- Human-readable table mode prints the same keys with empty values
  shown as `(empty)`.

### 2.3 Naming convention (flags vs JSON)

The CLI deliberately uses two conventions for the same logical names:

- **Flags use hyphens** (`--assembly-guide`-style names; folder enum
  values like `assembly-guide`). Matches Unix CLI convention.
- **JSON keys use underscores** (`"assembly_guide"`). Matches the
  data-format convention used by most JSON consumers (Python dicts,
  most JSON-schema generators).

Affected mapping today (the only case): `--folder assembly-guide` ↔
JSON key `"assembly_guide"`. The other three folder names (`pictures`,
`bom`, `others`) are single-word and identical in both surfaces.

This convention is documented in each verb's `--help` and tested
explicitly in the e2e suite (a JSON round-trip case asserts the
underscore key, an aux-add case asserts the hyphen flag).

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

### 3.4 Cover image embedding (PNG-only)

`embed_cover_image(Model&, fs::path src, CoverTarget target)`:

1. Validate `src` exists and is readable.
2. Read the first 8 bytes of `src` and require them to match the PNG
   signature exactly: `89 50 4E 47 0D 0A 1A 0A`. Anything else (JPG,
   WebP, BMP, truncated file, non-image) → `BadCoverImage`
   (exit 4 `bad_config`) with a message naming the offending path and
   the detected magic prefix in hex.
3. Copy bytes into the auxiliary temp dir at
   `.thumbnails/thumbnail_3mf.png` (canonical path per
   `bbs_3mf.cpp:205`). The filename extension is fixed at `.png`
   regardless of the source filename, which is safe because we just
   verified the source content is PNG.
4. Set `model.model_info->cover_file` (if `target==Info`) or
   `model.profile_info->ProfileCover` (if `target==Profile`) to the
   canonical relative path (`Auxiliaries/.thumbnails/thumbnail_3mf.png`,
   no leading slash — matches how the existing GUI exporter writes the
   pointer at `bbs_3mf.cpp:6802`).

If `target==Info` and `model.model_info == nullptr`, allocate it. Same
for `target==Profile` and `model.profile_info == nullptr`.

**Why PNG-only.** The exporter wires the OPC cover-thumbnail
relationship by exact string match against `_3MF_COVER_FILE`, which is
hard-coded to `.png` (`bbs_3mf.cpp:8513`). Embedding JPG bytes under
the `.png` filename would either (a) lose the OPC wiring if we changed
the extension to match content, or (b) produce a file whose content
disagrees with its extension. Both are foot-guns. Rejecting JPG at
parse time keeps the round-trip honest and the GUI cover-thumbnail
relationship intact. Re-encoding JPG→PNG is out of scope (would pull
in an image-decoding dependency; the user can do it with any tool).

### 3.5 Cover image clear (refcount)

`clear_cover_image(Model&, CoverTarget target)`:

1. If `target==Info`: null `model.model_info->cover_file` (set to `""`).
   If `target==Profile`: null `model.profile_info->ProfileCover`.
2. Check the *other* surface's pointer. If it is also empty (or its
   struct is nullptr), delete `Auxiliaries/.thumbnails/thumbnail_3mf.png`
   from the auxiliary temp dir if present; otherwise leave the embedded
   file in place (the other surface still owns it).
3. Idempotent: nulling an already-empty pointer is a no-op; deleting an
   already-absent file is a no-op. Exit 0 either way.

See § 2.1 for the worked example of the two-clear sequence.

### 3.6 Read path

`show` verbs and `aux list` call `load_bbs_3mf` and emit. No mutation,
no guard, no rename. Cheap.

## 4. Data flow per verb

### `project info show <file>`
- Load → read `model.model_info` → emit human table or JSON.
- `model_info == nullptr` → emit the full key set with empty-string
  values (see § 2.2 for the exact shape); exit 0.

### `project info set <file> [--title …] [--description …] [--cover IMG] …`
1. Parse flags; CLI11 rejects zero field-flags (`usage_error`, exit 1)
   via a "require at least one of --title/--description/…" group.
2. Load. Allocate `model_info` if nullptr.
3. For each supplied flag: assign to the matching `ModelInfo` string
   field, or call `embed_cover_image(.., CoverTarget::Info)` for `--cover`.
4. Store → guard → atomic rename.

### `project info clear <file> --field title,description,…`
1. Parse `--field` as comma list; validate each against
   `{title, description, license, copyright, cover}`. Unknown →
   `InvalidField` (exit 4 `bad_config`).
2. Load.
3. For each named field: null it (empty string for strings). For
   `cover`, call `clear_cover_image(.., CoverTarget::Info)` (§ 3.5) —
   refcount semantics: file delete is conditional on the profile
   surface also being empty.
4. Store → guard → atomic rename. Idempotent.

### `project profile show / set / clear`
Symmetric to `info`, applied to `model.profile_info`:

- `ProfileTile` (sic — typo preserved from upstream) ↔ `title`
- `ProfileDescription` ↔ `description`
- `ProfileCover` ↔ `cover`
- `ProfileUserId` ↔ `user_id` (read-only in show; not exposed in set/clear)
- `ProfileUserName` ↔ `user_name` (read-only in show; not exposed in set/clear)

`profile show` on `profile_info == nullptr` emits the full key set with
empty strings (§ 2.2).

`profile set --cover` reuses `embed_cover_image(.., CoverTarget::Profile)`
(§ 3.4). Set overwrites the shared image — see § 2.1.

`profile clear --field cover` reuses `clear_cover_image(.., CoverTarget::
Profile)` (§ 3.5) with the same refcount rule: the embedded file is
only deleted when both `info` and `profile` cover pointers are empty.

### `project aux list <file>`
- Load → walk the four bucket directories under
  `model.get_auxiliary_file_temp_path()` → emit table or JSON. No write.

### `project aux add <file> --folder F --file PATH [--name N] [--force]`
1. Validate `--folder` against the enum. CLI11's `CheckedTransformer`
   rejects unknown values with `usage_error` (exit 1) before our
   callback runs.
2. Stat `--file PATH`; missing/unreadable → `BadAuxFile`
   (exit 2 `file_not_found`).
3. Load.
4. Compute target name: `N` if given, else `basename(PATH)`. Sanitize
   per the full ruleset in § 2.1 (rejects path separators, `..`, null
   bytes, leading/trailing dots & whitespace, and Windows reserved
   names like `CON`, `COM1`, `LPT1`). Any rejection → `AuxNameError`
   (exit 4 `bad_config`) with a message naming the offending substring.
5. Collision check against the bucket subdir; present and not `--force` →
   `AuxCollisionError` (exit 5 `duplicate_name`).
6. Copy source bytes into `<bucket>/<name>` in the temp aux dir.
7. Store → guard → atomic rename.

### `project aux remove <file> --folder F --name N`
1. Validate `--folder` (CLI11; see `aux add` step 1).
2. Load.
3. `<bucket>/<name>` not present → throw `std::out_of_range`; mapped
   by `MutationExceptionMap` default to `unknown_reference` (exit 6).
4. Delete file from temp aux dir.
5. Store → guard → atomic rename.

### `project aux export <file> --folder F --name N --to PATH`
1. Validate `--folder`.
`aux export` is read-only — it doesn't go through `run_mutation`
(which calls `save_project`). It has its own try/catch chain in the
command callback:

1. Resolve `--to PATH`:
   - If `PATH` exists and is a directory → final destination is
     `PATH/<name>` (where `<name>` is the in-3mf basename from
     `--name`).
   - Otherwise → `PATH` is the final destination (file path).
   - In both cases, the parent directory of the final destination must
     exist; otherwise → `bad_config` (exit 4) with message
     `--to parent dir does not exist: <path>`. We do not create
     intermediate directories.
2. Load (`load_project`).
3. `<bucket>/<name>` not present in the 3mf → `unknown_reference`
   (exit 6) with message `aux file not found: <bucket>/<name>`.
4. Copy file from temp aux dir → final destination. **Overwrites
   existing destination files without prompting** (same posture as
   v2's `--output O`: the user named the destination path explicitly,
   so the write is consent). Any filesystem error during copy
   (permission denied, destination held open by another process on
   Windows, etc.) → `parse_failure` (exit 3) with the OS error
   message surfaced.

No model rewrite. No guard. No rename of the source 3mf.

## 5. Error handling

### 5.1 Exit-code contract

Reused from v2 (`src/cli/output.hpp`, `enum class ExitCode`, no code
changes). The v2 enum uses specific labels (`file_not_found`,
`duplicate_name`, `unknown_reference`, `invalid_state`, etc.) rather
than generic "BadInput"; this spec maps each new failure to the
existing label whose semantics match best:

| Code | v2 label              | Used here for |
|------|-----------------------|---------------|
| 0    | `ok`                  | success and idempotent no-ops |
| 1    | `usage_error`         | CLI11 rejects unknown / missing required flags (e.g. unknown `--folder` enum, zero field-flags on `set`); we let CLI11 handle these. Read-only verbs reject `--output` here. |
| 2    | `file_not_found`      | missing input 3mf (`check_input_exists` in `io.hpp`); `--file PATH` for `aux add` unreadable or absent (`BadAuxFile`). |
| 3    | `parse_failure`       | `load_project` failure on a corrupt 3mf; any uncaught `std::exception` from the load path. |
| 4    | `bad_config`          | `--cover IMG` not a valid PNG (`BadCoverImage`); `--field NAME` not in the per-surface allowed set (`InvalidField`); `--name N` rejected by sanitization (`AuxNameError`); `--to PATH` parent not writable. |
| 5    | `duplicate_name`      | `aux add` collision without `--force` (`AuxCollisionError`). |
| 6    | `unknown_reference`   | `aux remove` / `aux export` for an absent name (raised as `std::out_of_range`, mapped by `MutationExceptionMap` default). |
| 8    | `invariant_violation` | post-save guard rejection (`verify_relationships`, etc. — v2 behaviour, untouched). |

New exception classes added next to existing
`PlacementFailure`/`BadConfigError`/`DuplicateNameError` in
`src/cli/project_ops.hpp` (or a sibling `project_tab_ops.hpp` — see
§ 3.1):

| Class                | Maps to       | Used for |
|----------------------|---------------|----------|
| `BadCoverImage`      | `bad_config`  | `embed_cover_image` rejects non-PNG content. Message includes offending path + detected magic prefix in hex. |
| `BadAuxFile`         | `file_not_found` | `aux_add` source `--file` missing or unreadable. Distinct from `BadCoverImage` so messages can be targeted. |
| `InvalidField`       | `bad_config`  | `info_clear`/`profile_clear` rejects an unknown field name. Message lists the legal names for that surface. |
| `AuxNameError`       | `bad_config`  | `aux_add` rejects an unsafe `--name N` (path separators, reserved names, etc.). Message names the offending substring. |
| `AuxCollisionError`  | `duplicate_name` | `aux_add` target name already present in the bucket and `--force` not set. |

Aux-name "not found" on `remove`/`export` continues to use stdlib
`std::out_of_range` (matches the existing `unknown_reference` default
in `MutationExceptionMap`). No new class needed.

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

Total new cases: **~40**. Suite grows from current ~155 to ~195.
Naming follows `tests/cli/{unit,e2e,roundtrip}/`.

### 6.1 Unit tests (~24 cases)

File: `tests/cli/unit/project_ops_test.cpp`

- `info_set`: sets all five fields; batches multiple flags;
  `model_info==nullptr` triggers allocation; idempotent re-set.
- `info_clear`: single field; multiple comma-separated; idempotent on
  already-empty (no error, no file delete attempted).
- `profile_set` / `profile_clear`: symmetric coverage of the above.
- `profile_show` JSON shape includes `user_id` and `user_name` read
  back from `ProfileUserId`/`ProfileUserName` (regression for § 1
  scope clarification).
- `embed_cover_image`: PNG accepted (first 8 bytes match
  `89 50 4E 47 0D 0A 1A 0A`); JPG explicitly rejected with
  `BadCoverImage` (not silently re-encoded); non-image rejected;
  missing src rejected; both targets (Info, Profile) point at the
  same canonical path and the second `set` overwrites the first's
  bytes.
- `clear_cover_image` (the refcount helper, § 3.5): four cases —
  - only Info pointer set → clear Info → file deleted; pointer null.
  - only Profile pointer set → clear Profile → file deleted; pointer
    null.
  - both pointers set → clear Info → file **kept**; Info pointer null,
    Profile pointer intact.
  - both pointers set → clear Info then clear Profile → file deleted
    on the second clear; both pointers null.
- `aux_add` sanitization (§ 2.1 ruleset): table-driven cases reject
  empty, `/`, `\`, null byte, `.`, `..`, leading `.`, trailing `.`,
  leading whitespace, trailing whitespace, `CON`, `con.png` (case-
  insensitive), `COM1`, `LPT9`; happy cases accept `model.stl`,
  `assembly_step_1.png`, `Bill of Materials.pdf`.
- `aux_add`: happy path; `--name` rename; collision without `--force`
  → throws `AuxCollisionError`; collision with `--force` → success;
  collision with `--force` on byte-identical file → success
  (idempotent). Unknown `--folder` is rejected by CLI11 at the parse
  layer, not by the op — the op accepts a typed `AuxFolder` enum.
- `aux_remove`: happy path; missing name → throws `std::out_of_range`
  (the command layer maps this to `unknown_reference`).
- `aux_list`: empty (returns four empty bucket arrays, not error);
  populated bucket walk; JSON shape stable (keys always present even
  when empty, per § 2.2).
- `aux_export`: happy path with file `--to`; happy path with directory
  `--to` (writes to `dir/<name>`, regression for § 4 directory case);
  missing name → throws `std::out_of_range`; non-existent `--to`
  parent → throws `std::invalid_argument`; destination file already
  exists → silently overwritten (consent via explicit `--to`).

### 6.2 E2E tests (~13 cases)

File: `tests/cli/e2e/project_e2e_test.cpp`. Process-level invocations
of `orca-cli`, exit-code + stdout assertions.

- `info set` happy path → `info show --json` parses and round-trips.
- `info set` with zero field flags → exit 1 (`usage_error`, raised by
  CLI11 or the do_info_set guard).
- `info set --cover hero.png` → `info show --json` reports canonical
  cover path `Auxiliaries/.thumbnails/thumbnail_3mf.png`.
- `info set --cover cover_smoke.jpg` → exit 4 (`bad_config`) with
  `BadCoverImage`; input 3mf untouched.
- `info clear --field title,description` → subsequent `info show --json`
  reports both empty.
- `info show --json` on a pristine 3mf (no `model_info`) emits the
  full key set with empty-string values (per § 2.2); exit 0.
- `profile set` + `profile show` parity case; `profile show --json`
  includes `user_id` and `user_name` keys.
- JSON-vs-flag naming convention: `aux add --folder assembly-guide
  --file X.pdf` → `aux list --json` emits the file under the key
  `"assembly_guide"` (underscore). Regression for § 2.3.
- `aux add --folder pictures --file X.png` → `aux list` reports it
  under pictures.
- `aux add` collision without `--force` → exit 5; with `--force` →
  exit 0.
- `aux add --name CON.png` → exit 4 (Windows reserved name rejected
  per § 2.1).
- `aux remove` happy + missing-name (exit 6).
- `aux export --to FILE` → file byte-equal to original source.
- `aux export --to DIR/` (existing directory) → file written to
  `DIR/<name>`, byte-equal to original source.
- `aux export --to NONEXISTENT_DIR/sub.png` → exit 4 (parent dir
  missing).
- `--output O` honoured on each mutating verb (does not overwrite input).

### 6.3 Roundtrip tests (~3 cases)

File: `tests/cli/roundtrip/project_roundtrip_test.cpp`

- Reference 3mf → `info set` (all fields) → `profile set` (all fields)
  → `aux add` (one per folder) → save → reload → assert state.
  Verifies edits survive a full pack/unpack cycle and don't trip the v2
  runtime guard.
- Reference 3mf → `info set --cover cover_smoke.png` → save → reload →
  assert (a) `model_info->cover_file` points at
  `Auxiliaries/.thumbnails/thumbnail_3mf.png` AND (b) the embedded file
  exists in the auxiliary temp dir AND (c) its bytes are byte-equal to
  `cover_smoke.png`. Regression for § 3.4 — exercises the
  PNG-bytes-survive-pack/unpack path that § 6.3's other cases skip.
- Reference 3mf → `aux add` → `aux remove` → reload → assert file gone
  and bucket empty (regression for half-baked removal).

### 6.4 Manual smoke recipe

Added to `docs/cli/manual-test.md` as **Phase 10**:

```
1. Copy reference 3mf to TEMP.
2. orca-cli project info set    --title "Smoke" --description "..." --cover hero.png
3. orca-cli project profile set --title "Profile Smoke" --cover hero.png
4. orca-cli project aux add     --folder assembly-guide --file instructions.txt
5. orca-cli project aux add     --folder pictures       --file photo.png
6. Open in OrcaSlicer; Project tab should render:
   - model title/description/cover
   - profile title/cover
   - instructions.txt under Assembly Guide
   - photo.png under Pictures
```

This is the GUI gate that catches things tests miss; it caught two
production bugs in v2 per the project memory.

### 6.5 New fixtures

Three tiny files committed to `tests/cli/fixtures/`:

- `cover_smoke.png` (67 bytes; valid PNG, 1×1 transparent) — happy
  path for `info set --cover` / `profile set --cover`.
- `cover_smoke.jpg` (~125 bytes; valid JPG, 1×1) — used **only** to
  assert the rejection path (`BadCoverImage`). Per § 3.4 the CLI never
  accepts JPG; the fixture exists so the rejection test isn't shipping
  a hand-rolled JPG byte literal.
- `assembly_smoke.txt` (~400 bytes; plain UTF-8 text) — happy path
  for `aux add --folder assembly-guide`. The aux pipeline is
  byte-agnostic (it copies bytes verbatim into the chosen bucket),
  so a text file gives identical coverage to a PDF while avoiding the
  fragility of hand-rolling a minimal valid PDF (xref byte offsets
  are position-sensitive and an invalid PDF would still pass the CLI
  tests but fail the Phase 10 GUI smoke).

Total <1 KB added to the repo.

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
