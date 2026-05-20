# orca-cli `object merge-parts` — Design (WIP)

**Status:** ⚠️ **BRAINSTORMING INCOMPLETE.** Sections 2-5 have NOT been brainstormed yet.
Section 1 (CLI surface) is approved. Resume from Section 2 (Behaviour semantics)
before writing an implementation plan. **Do not invoke writing-plans against this
document until brainstorming is complete and the user has approved every section.**

**Date opened:** 2026-05-20
**Scope:** v4 of orca-cli — one new verb (`object merge-parts`)
**Pairs with:** `object split-to-parts` (shipped in Phase 8, see
`docs/superpowers/specs/2026-05-20-orca-cli-split-to-parts-design.md`)

---

## How to resume

1. Read this document end-to-end.
2. Invoke `superpowers:brainstorming` skill.
3. Re-state to the brainstormer: "We're resuming the merge-parts brainstorm.
   Section 1 (CLI surface) is approved; we left off at Section 2."
4. Walk Sections 2-5 (Behaviour, Edge cases, Tests, Docs) one at a time, with
   the user approving each before moving on.
5. When all sections are approved, the WIP document gets renamed to remove
   the `-WIP` suffix and the status banner gets removed.
6. Then (and only then) invoke `superpowers:writing-plans`.

---

## Goal

Provide a CLI command to combine a subset of ModelVolumes (parts) within a
ModelObject into a single merged ModelVolume. Natural inverse of
`object split-to-parts`: after splitting a mesh into N components, the user
can selectively re-merge subsets to reduce part count or unify per-part
filament assignments.

## Non-goals (out of scope for this spec)

- **Cross-object merge.** Combining parts from different ModelObjects. Different
  semantics (which object survives? which transform wins?). Future spec.
- **CSG (boolean) union.** True geometric union via `MeshBoolean::cgal::plus`.
  v1 uses simple triangle-list concatenation (matches OrcaSlicer GUI's current
  `merge_volumes` behaviour). Future verb `object union-parts` could expose CSG
  separately if needed.
- **Merge-all-parts** as a distinct verb. The user can always pass every part
  name to `--parts`; we don't add a `--all` sugar in v1.
- **Pattern/regex selection** (`--match X_[135]`). v1 uses comma-separated names.
- **Renaming a single non-merged volume** (no list, just rename). Out of scope.

---

## Decisions made (Section 0 — Brainstorm Q&A log)

These four design questions were answered before the WIP save:

### Q1: Concatenation vs CSG union for the mesh combine operation

**Answer: Concatenation** (`TriangleMesh::merge`).

For disjoint parts (the normal case from split-to-parts), concatenation is
identical to CSG union — split-to-parts produces disconnected components by
definition. For overlapping parts, the merged volume contains interior
surfaces; the slicer treats them as one logical object but still "sees" those
shells. This matches OrcaSlicer GUI's current `ModelObject::merge_volumes`
implementation (`src/libslic3r/Model.cpp:2179-2189` and the active branch in
`Model.cpp:2211-2221`, with the CSG branch `#if 0`'d out at line 2222).

CSG union (`MeshBoolean::cgal::plus`) is rejected for v1 because:
- Slow on complex meshes (seconds to minutes)
- Can fail on non-manifold / self-intersecting input
- Requires CGAL — adds compile/test surface
- For the dominant use case (post-split disjoint components), produces
  identical output to concatenation

### Q2: How to select which parts to merge

**Answer: Comma-separated names list** (`--parts X_1,X_3,X_5`).

Mirrors the existing `--part X_1` (singular) convention from `set-filament`
(Phase 8). Names are stable — set by `split-to-parts` as `{object}_N` —
and discoverable via `inspect`. The typical workflow:

```
orca-cli inspect file.3mf                               # see part names
orca-cli object merge-parts file.3mf --name X \
    --parts X_1,X_3,X_5 --into X_main
```

Index-based (`--indices 1,3,5`) and pattern-based (`--match 'X_[135]'`)
selection were both rejected. Indices shift after every mutation, making
scripts fragile. Patterns add parsing complexity and footgun potential.

### Q3: How to name the merged part

**Answer: Required `--into <name>` flag.**

User specifies the merged volume's name explicitly. No auto-generation. If
`--into` is missing, the command exits with `usage_error` (exit 1). This is
the verbose-but-explicit style matching the rest of the orca-cli surface.

### Q4: Filament inheritance when sources have different filament slots

**Answer: Smart default + `--filament N` override.**

- If all source parts have the same effective extruder (whether per-volume
  or inherited from object-level), the merged part inherits that filament.
  No `--filament` flag needed in the common case.
- If sources have different extruders AND `--filament N` is supplied:
  merged part takes N.
- If sources have different extruders AND `--filament` is missing: refuse
  with `invalid_state` (exit 7) and a message instructing the user to
  either unify filaments first via `set-filament` or pass `--filament N`.
- Allowing `--filament N` when sources agree is allowed (explicit override).

Range validation: same as `set-filament` — 1..`filament_slot_count`.

Per-volume non-extruder config keys (e.g., `wall_loops`) are out of scope
for this filament question; their handling is a Section 2 brainstorm item.

---

## Section 1 — CLI surface (APPROVED)

### New verb: `object merge-parts`

```
orca-cli object merge-parts <file> --name X --parts X_1,X_3,X_5 \
    --into X_main [--filament N] [--output O]
```

Merges the named subset of ModelVolumes within ModelObject `X` into a single
new ModelVolume named via `--into`. The remaining (non-listed) volumes stay
as-is. Mesh combination is **triangle concatenation**
(`TriangleMesh::merge`), matching what OrcaSlicer's GUI `merge_volumes`
does today.

### Flag semantics

- `--name X` — the parent ModelObject. Required.
- `--parts X_1,X_3,X_5` — comma-separated list of source volume names.
  Required. Must contain ≥ 2 entries; all must exist on the object; no
  duplicates.
- `--into X_main` — name for the resulting merged ModelVolume. Required.
  Must not collide with an existing volume name on the object *unless* the
  colliding name is itself one of the sources being consumed (in which case
  the existing volume is consumed and replaced).
- `--filament N` — optional. Required only when the source parts have
  different effective extruder assignments; otherwise the merged part
  inherits the common filament. Range 1..filament_slot_count.
- `--output O` — standard side-car semantics, like every other mutating
  command.

### Inspect interaction

After merge, `inspect --json` reports the object's `volumes` array with the
merged volume at the position of the FIRST source (libslic3r insertion-order
semantics — the merged volume replaces the first source in place; the other
sources are removed from the volumes vector).

Example before merge:
```
volumes: [X_1, X_2, X_3, X_4]  (extruders 1, 1, 2, 1)
```
After `merge-parts --parts X_1,X_3 --into X_main --filament 1`:
```
volumes: [X_main, X_2, X_4]    (extruders 1, 1, 1)
```

### What's NOT changing

- Existing `split-to-parts`, `set-filament`, `add`, `remove`, `list`,
  `inspect` — unchanged behaviour, unchanged exit codes.
- No CSG / boolean union path in v1.
- No cross-object merge in v1.

---

## Section 2 — Behaviour semantics (NOT STARTED)

Open questions to brainstorm next:

- **Implementation locus.** Do NOT delegate to `ModelObject::merge_volumes(indices)`
  directly — it is buggy:
  1. Mutates `this->volumes[i]->reset_mesh()` (destroys mesh on the *original*
     ModelObject) while building a *cloned* return object — corrupts the
     caller's state.
  2. Inside the loop that assigns `vol->name` and `vol->config`, the
     reassignment happens once per matched index — final state is the LAST
     matched source's name/config, not the first or a deliberate choice.
  3. Returns a `ModelObjectPtrs` (clones the entire ModelObject), which is
     wrong for an in-place mutation API.

  Implement merge logic directly in `src/cli/project_ops.cpp` using
  `TriangleMesh::merge` + manual volume-vector splicing.

- **Volume transformations.** Each `ModelVolume` carries a `get_matrix()`
  (placement transform relative to the ModelObject). Source volumes from
  `split-to-parts` have identity matrices (libslic3r resets them after
  split). But a user could in theory have multi-volume objects with
  non-identity matrices (e.g., from external mesh edits). Two choices:
  - Bake each source's matrix into its mesh before concat, then set merged
    volume's matrix to identity.
  - Refuse the merge if any source has a non-identity matrix.

  Recommended: bake-in (matches what libslic3r's buggy `merge_volumes`
  attempts at line 2216).

- **Source volume index for placement.** Insert merged volume at the position
  of the FIRST source in `obj.volumes`. Remove the other sources. The
  merged volume's order in the inspect output reflects this.

- **Per-volume config (non-extruder keys).** Sources may have per-volume
  config beyond `extruder` (e.g., `wall_loops`). v1 rule:
  - If all sources agree on a key, merged inherits it.
  - If sources differ on any non-extruder key: silently take from FIRST
    source (or refuse — to decide in brainstorm).
  - User can always edit afterwards via `config set --object X --part X_main`
    (note: `config set --part` doesn't exist yet — out of scope here).

- **Source attribution (Bug C class).** The merged volume needs
  `source.input_file` populated. Use the FIRST source's `source.input_file`
  (sources from the same split share an STL path). Stamp via the existing
  `stamp_source_if_missing` helper from T5 of split-to-parts.

- **Save pipeline invariants.** No new invariant guard needed. The existing
  `verify_relationships`, `verify_plate_thumbnails`, and
  `verify_vector_config_roundtrip` are insensitive to volume count changes.

- **`obj.invalidate_bounding_box()`** must be called after the volume swap.

---

## Section 3 — Edge cases and exit codes (NOT STARTED)

Open questions:

- `--parts` with 0 entries → `usage_error` (exit 1).
- `--parts` with 1 entry → `usage_error` (exit 1, message: "merge-parts
  requires at least 2 source parts").
- `--parts` with duplicate entries → `usage_error` (exit 1).
- A source name doesn't exist on the object → `unknown_reference` (exit 6).
- `--into <name>` collides with an existing volume that is NOT in the source
  list → `duplicate_name` (exit 5).
- `--into <name>` matches one of the sources (consumed) → allow (merged
  volume reuses the source's name).
- Sources have different extruders AND `--filament` missing → `invalid_state`
  (exit 7).
- `--filament N` out of range → `unknown_reference` (exit 6), same as
  `set-filament` today.
- Unknown ModelObject (`--name X` doesn't exist) → `unknown_reference`
  (exit 6).
- Source volume is not `MODEL_PART` (it's a modifier, support enforcer,
  etc.) → `invalid_state` (exit 7). Merging modifier meshes isn't meaningful.
- All sources together produce an empty mesh (all were empty) → `invalid_state`
  (exit 7, message: "cannot merge: all source parts have empty meshes").

---

## Section 4 — Tests (NOT STARTED)

Open questions:

- **Unit tests** in `tests/cli/unit/test_project_ops.cpp`:
  - happy path: 2-part Layer A two_cubes → merge → 1 part
  - 3-source subset on Layer B box_with_text (7 parts → merge 3 → 5 parts)
  - refuse single-entry `--parts` list
  - refuse duplicate names in list
  - refuse unknown source name
  - refuse `--into` collision with non-source name
  - allow `--into` collision when target IS a source name
  - filament inheritance: all-agree case (no `--filament` needed)
  - filament conflict + `--filament N` override
  - filament conflict + missing `--filament` refused
  - source attribution preserved (Bug C lock-in)

- **E2E tests** in `tests/cli/e2e/test_merge.cpp`:
  - happy path + `inspect --json` confirms 1 fewer volume + correct name
  - anti-cases for each exit code

- **Roundtrip** in `tests/cli/roundtrip/test_merge.cpp`:
  - merged volume + name + extruder survive load/save

- **Archive invariants:** existing `assert_parts_have_source_file` runs on
  every per-volume e2e (Bug C class), confirming the merged volume carries
  source attribution.

---

## Section 5 — Documentation (NOT STARTED)

Open questions:

- `docs/cli/manual-test.md` Phase 9 manual smoke recipe.
- `docs/cli/status.md` Phase 9 status block.
- Update cumulative P8 recipe to also exercise merge-parts.
- Memory updates after implementation.

---

## Open items for the resumed brainstorm

1. **Volume transformation policy** — bake-in or refuse non-identity matrices.
2. **Non-extruder per-volume config conflict** — silently first-source or refuse.
3. **`--into` name collision with source** — explicit edge case worth confirming.
4. **Test fixture for filament-conflict cases** — need a multi-extruder reference
   project to actually have >1 filament slot available. Reference 3mf
   `temp_project_for_orca_slicer.3mf` filament count needs verification.
5. **Empty-mesh source handling** — silently drop empty sources or refuse?

---

## Resume checklist

When resuming:

- [ ] Read this WIP doc end-to-end.
- [ ] Invoke `superpowers:brainstorming`.
- [ ] Section 2 brainstorm — Behaviour semantics (the 5 open items above).
- [ ] Section 3 brainstorm — Edge cases + exit codes (likely just confirm
      the proposed list).
- [ ] Section 4 brainstorm — Tests + fixtures.
- [ ] Section 5 brainstorm — Docs.
- [ ] Rename file to `2026-05-20-orca-cli-merge-parts-design.md` (drop `-WIP`).
- [ ] Remove the status banner at the top.
- [ ] Commit final spec.
- [ ] User reviews and approves.
- [ ] Invoke `superpowers:writing-plans`.
