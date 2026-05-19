#pragma once
// Deterministic grid placement helpers for `orca-cli object add` (P3).
//
// Two concerns kept deliberately small and pure so they are unit-testable
// without dragging in any of the project IO or Model machinery:
//
//   plate_origin_offset() -- the GUI lays out plates in a sqrt-based 2D
//   grid (cols = ceil(sqrt(N))), each plate having its own bed origin.
//   Returns the X/Y world-space offset of plate `plate_index` relative
//   to plate 0. Z is always 0.
//
//   place_in_plate() -- within a single plate's bed, lay out the n-th
//   added object on a sqrt-based grid anchored at bed-min + a 10 mm
//   margin. Returns the desired center position so the caller can
//   `inst->set_offset(pos)`.
//
// In add_object() the two are composed: target_bed = bed shifted by
// plate_origin_offset(target, total, stride_x, stride_y), then
// place_in_plate(target_bed, base_idx + i, bbox_size) for each new
// instance. See src/cli/project_ops.cpp::add_object.
#include <libslic3r/Point.hpp>
#include <libslic3r/BoundingBox.hpp>

namespace orca_cli {

using Slic3r::Vec3d;
using Slic3r::BoundingBoxf3;

// Returns the X/Y offset of plate `plate_index` (0-based) in the GUI's
// sqrt-based 2D plate grid. Z is always 0.
//   total_plates >= 1; plate_index < total_plates.
// For total_plates <= 1 (or plate_index <= 0) the offset is the zero
// vector.
Vec3d plate_origin_offset(int plate_index, int total_plates,
                          double stride_x, double stride_y);

// Within-plate grid placement. Returns the center position of object N
// (0-based) inside the plate's bed. Anchored at bed.min + a 10 mm
// margin. Square grid: cols = ceil(sqrt(slot + 1)). Cell width / height
// is max(bbox_size + margin, default_cell). Z is bed.min.z() so the
// object sits on the bed.
Vec3d place_in_plate(const BoundingBoxf3& bed, int idx_in_plate,
                     const Vec3d& bbox_size);

} // namespace orca_cli
