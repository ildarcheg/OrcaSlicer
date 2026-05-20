# orca-cli `object merge-parts` — Design

**Date:** 2026-05-20
**Scope:** v4 of orca-cli — one new verb (`object merge-parts`)
**Pairs with:** `object split-to-parts` (shipped in Phase 8, see
`docs/superpowers/specs/2026-05-20-orca-cli-split-to-parts-design.md`)

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

## Decisions made (Brainstorm Q&A log)

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
`--into` is missing or empty, the command exits with `usage_error` (exit 1).
Verbose-but-explicit style matching the rest of the orca-cli surface.

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

### Q5: Volume transformation policy (Section 2 brainstorm)

**Answer: Bake-in.**

For each source, transform its mesh by `get_matrix()` before concatenation,
then set the merged volume's matrix to identity. Matches the intent of
libslic3r's `ModelObject::merge_volumes` at `Model.cpp:2216`
(`mesh_.transform(volume_matrix, true)`). Volumes from `split-to-parts`
always carry identity matrices — this rule only affects multi-volume
objects whose volumes were authored elsewhere (e.g. external mesh edits).
Always works; no user friction; no need for a per-volume matrix verb in v1.

### Q6: Non-extruder per-volume config conflict (Section 2 brainstorm)

**Answer: Per-key inherit; refuse on conflict.**

Source ModelVolumes carry a `ModelConfigObject` that may contain per-volume
keys beyond `extruder` (e.g. `wall_loops`, `top_solid_layers`,
`wall_filament`). Rule:

- For each key that appears on any source: if every source either lacks the
  key or carries the same value, the merged volume inherits that value.
- If any non-extruder key has conflicting values across sources: refuse
  with `invalid_state` (exit 7), and the message lists the conflicting
  key names so the user knows what to unify before retrying.

In the dominant workflow (merge after `split-to-parts`) every source
inherits identical config from the parent ModelObject, so this case is
rare. It only fires on volumes hand-edited via the GUI or external tools.

### Q7: Empty-mesh source handling (Section 2 brainstorm)

**Answer: Silently skip empties; require ≥2 non-empty sources.**

- Sources whose `mesh().empty()` returns true are dropped from the
  concatenation. Matches libslic3r's own `ModelObject::merge()` skip
  behaviour at `Model.cpp:2181-2183`.
- If fewer than 2 non-empty sources remain after the drop, refuse with
  `invalid_state` (exit 7, message: "merge requires ≥2 non-empty source
  meshes").
- If all source meshes were empty, refuse with `invalid_state` (exit 7,
  message: "all source parts have empty meshes").

---

## Section 1 — CLI surface

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
- `--into X_main` — name for the resulting merged ModelVolume. Required
  and non-empty. Must not collide with an existing volume name on the
  object *unless* the colliding name is itself one of the sources being
  consumed (in which case the existing volume is consumed and replaced).
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

## Section 2 — Behaviour semantics

### Implementation locus

Implement merge logic **directly** in `src/cli/project_ops.cpp` using
`TriangleMesh::merge` + manual volume-vector splicing. Do **not** delegate
to `ModelObject::merge_volumes(indices)` — it has three confirmed bugs:

1. Mutates `this->volumes[i]->reset_mesh()` (destroys mesh on the *original*
   ModelObject, `Model.cpp:2217`) while building a *cloned* return object —
   corrupts the caller's state.
2. In the loop at `Model.cpp:2233-2237`, `vol->name = ... + "_merged"` and
   `vol->config.assign_config(...)` execute once per matched index — final
   state is the LAST matched source's name/config, not a deliberate choice.
3. Returns a `ModelObjectPtrs` (clones the entire ModelObject), which is
   wrong for an in-place mutation API.

A new project_ops API will be added with signature similar to:
```cpp
void merge_object_parts(ModelObject& obj,
                        const std::vector<std::string>& source_part_names,
                        const std::string& merged_part_name,
                        std::optional<int> filament_override);
```
Throws library-defined exception types that the CLI surface translates to
the appropriate exit codes (see Section 3).

### Volume transformations (bake-in)

For each non-empty source `v`:
1. Copy `v->mesh()` into a fresh `TriangleMesh`.
2. Apply `v->get_matrix()` via `TriangleMesh::transform(M, fix_left_handed=true)`.
3. Append the result via `merged_mesh.merge(transformed)`.

Set the merged volume's matrix to identity (`Geometry::Transformation()`).

### Source volume placement

Insert the merged volume at the position of the FIRST source in
`obj.volumes`. Erase all source volumes from the vector. This preserves
inspect-output stability (the merged volume's index ≤ the indices of the
sources it consumed).

### Per-volume non-extruder config

For each key encountered on any source's `ModelConfigObject`:
- If all sources that carry the key agree on its value AND sources that
  don't carry the key would inherit the same value from object-level
  config, the merged volume inherits that value.
- Otherwise refuse with `invalid_state` (exit 7), message listing keys.

The `extruder` key is handled separately by Q4's smart default + override.

### Source attribution (Bug C class)

The merged volume's `source.input_file` is stamped from the FIRST source's
`source.input_file` via the existing `stamp_source_if_missing` helper
introduced for `split-to-parts` (Phase 8 task T5). Sources from a common
split-to-parts share an STL path; for hand-authored multi-volume objects,
the first source's attribution is the most-likely-meaningful choice.

### Save-pipeline invariants

No new invariant guard. The existing post-`store_bbs_3mf` runtime guard
(`verify_relationships`, `verify_plate_thumbnails`,
`verify_vector_config_roundtrip`) is insensitive to volume count changes
and continues to apply unchanged.

### Bounding box

Call `obj.invalidate_bounding_box()` after the volume swap.

---

## Section 3 — Edge cases and exit codes

| # | Case | Exit | Symbol |
|---|------|------|--------|
| 1 | `--parts` missing or empty | 1 | `usage_error` |
| 2 | `--parts` has 1 entry | 1 | `usage_error` ("requires ≥2 source parts") |
| 3 | `--parts` has duplicate names | 1 | `usage_error` |
| 4 | `--into` missing or empty | 1 | `usage_error` |
| 5 | Source name not on object | 6 | `unknown_reference` |
| 6 | `--name X` ModelObject not found | 6 | `unknown_reference` |
| 7 | `--filament N` out of range (not 1..filament_slot_count) | 6 | `unknown_reference` |
| 8 | `--into` collides with a NON-source volume name | 5 | `duplicate_name` |
| 9 | `--into` matches one of the SOURCE names | ALLOW | (source consumed, name reused) |
| 10 | Sources have differing extruders AND `--filament` missing | 7 | `invalid_state` |
| 11 | Any source volume is not `MODEL_PART` (modifier, support enforcer, etc.) | 7 | `invalid_state` |
| 12 | All source meshes are empty | 7 | `invalid_state` ("all source parts have empty meshes") |
| 13 | After dropping empty sources, <2 non-empty remain | 7 | `invalid_state` ("merge requires ≥2 non-empty source meshes") |
| 14 | Per-volume non-extruder config keys diverge across sources | 7 | `invalid_state` (message lists conflicting keys) |

---

## Section 4 — Tests

Coverage target: every refusal class exercised at unit level AND e2e level,
plus filament/config inheritance, bake-in transform, Bug C lock-in,
insertion position, and Layer B realistic-mesh chain.

### Unit tests — `tests/cli/unit/test_project_ops.cpp`

1. Happy 2-source merge on Layer A `two_cubes.stl`: split-to-parts → merge → 1 volume.
2. Refuse unknown source name → throws `unknown_reference` (CLI exit 6).
3. Refuse `--into` collision with non-source name → throws `duplicate_name` (exit 5).
4. Allow `--into` matching a source name (consumed, name reused).
5. Refuse: any source volume is not `MODEL_PART` → `invalid_state` (exit 7).
6. Refuse: <2 non-empty meshes after dropping empties → `invalid_state` (exit 7).
7. Refuse: filament conflict without `--filament` override → `invalid_state` (exit 7).
8. Filament conflict + `--filament N` override applied to merged volume.
9. Filament inherits when all sources agree (no `--filament` needed).
10. Per-volume config inherits when sources agree.
11. Refuse: per-volume non-extruder config conflict → `invalid_state` (exit 7), message lists keys.
12. Bake-in: source with non-identity `get_matrix()` → merged geometry reflects the transform (AABB sanity).
13. Source attribution (Bug C lock-in): merged volume `source.input_file` matches first source.
14. Merged volume inserted at FIRST source's position; other sources erased.

### E2E tests — `tests/cli/e2e/test_merge.cpp`

1. Happy path + `inspect --json` confirms volume count -1 (for 2-source merge), merged name present, filament correct.
2. End-to-end Layer B realistic mesh: object add `box_with_text.stl` → split-to-parts (7 parts) → `set-filament --part` differential → `merge-parts` subset → inspect confirms merged result + non-merged parts intact. SKIP-when-absent if Layer B fixture missing.
3. Exit 1: `--parts` missing.
4. Exit 1: `--parts` has 1 entry.
5. Exit 1: `--parts` contains duplicate names.
6. Exit 1: `--into` missing or empty.
7. Exit 5: `--into` collides with a non-source volume.
8. Exit 6: source name not on object.
9. Exit 6: `--name X` ModelObject not found.
10. Exit 6: `--filament N` out of range.
11. Exit 7: filament conflict + `--filament` missing.
12. Exit 7: per-volume non-extruder config conflict (message lists keys).
13. `--output O` sidecar — input file untouched, output file holds merged result.

### Roundtrip — `tests/cli/roundtrip/test_merge.cpp`

1. Volume count, merged volume name, merged volume extruder, and merged
   per-volume config survive load → save → load → save.

### Archive invariants

The existing `assert_parts_have_source_file` helper (from Phase 8) runs on
every per-volume e2e test, confirming the merged volume carries
`source.input_file` after save/load (Bug C class lock-in).

### Fixture notes

- Layer A: existing `tests/cli/fixtures/two_cubes.stl` (committed) is
  sufficient for the happy 2-source merge and the bake-in / per-vol-config
  cases (constructed in-test via direct `ModelObject` manipulation when
  needed).
- Layer B: existing `box_with_text.stl` (under `slicer_tamplates\`) drives
  the realistic 7-part chain; SKIP-when-absent.
- Multi-filament: the reference 3mf `temp_project_for_orca_slicer.3mf`
  must have `filament_slot_count >= 2` for filament-conflict tests to be
  meaningful. The implementation plan must verify this at the first task
  and either confirm or arrange a test-only multi-filament reference.

---

## Section 5 — Documentation

1. **`docs/cli/manual-test.md` Phase 9 section** — PowerShell smoke recipe
   for `object merge-parts`: setup with the reference 3mf, happy-path
   merge, then one-line anti-cases per exit code. Mirrors the Phase 8
   structure at `manual-test.md` lines 283+.
2. **`docs/cli/status.md` Phase 9 block** — status checklist, CLI surface
   entry, manual GUI verification line. Mirrors the Phase 8 block at
   `status.md` line 380.
3. **Cumulative recipe extension** — chain Phase 8's split-to-parts recipe
   forward: object add `box_with_text.stl` → split-to-parts (7 parts) →
   `set-filament --part` on a subset → `merge-parts` a different subset →
   GUI open verifies merged volume + filament assignments + remaining
   parts all survive.
4. **Memory updates after implementation** — new `project_orca_cli_v4.md`
   memory recording ship status, test-count delta, GUI verification, and
   the NOT-pushed status per existing autonomous-execution feedback.

CLI `--help` text update is an implementation detail handled by the
writing-plans phase, not a separate doc deliverable here.
