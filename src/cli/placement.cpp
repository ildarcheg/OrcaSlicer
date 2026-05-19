// placement.cpp -- deterministic per-plate / within-plate grid math.
// See placement.hpp for the design summary.
#include "placement.hpp"

#include <algorithm>
#include <cmath>

namespace orca_cli {

Vec3d plate_origin_offset(int plate_index, int total_plates,
                          double stride_x, double stride_y)
{
    if (total_plates <= 1 || plate_index <= 0)
        return Vec3d::Zero();

    int cols = int(std::ceil(std::sqrt(double(total_plates))));
    if (cols < 1) cols = 1;

    int col = plate_index % cols;
    int row = plate_index / cols;

    // Y is negated because the GUI stacks plate rows top-to-bottom while
    // bed coordinates grow upward. Bug (b) from the v1.1 backup branch.
    return Vec3d(double(col) * stride_x,
                 -double(row) * stride_y,
                 0.0);
}

Vec3d place_in_plate(const BoundingBoxf3& bed, int idx_in_plate,
                     const Vec3d& bbox_size)
{
    constexpr double margin = 10.0;

    const int slot = std::max(0, idx_in_plate);
    int cols = int(std::ceil(std::sqrt(double(slot + 1))));
    if (cols < 1) cols = 1;
    const int col = slot % cols;
    const int row = slot / cols;

    // Cell size = max(bbox extent + margin, a default 20 mm cell) so tiny
    // objects don't collapse on top of each other. Deterministic per
    // (slot, bbox_size).
    const double cell_x = std::max(bbox_size.x() + margin, 20.0);
    const double cell_y = std::max(bbox_size.y() + margin, 20.0);

    const double x = bed.min.x() + margin + double(col) * cell_x + bbox_size.x() / 2.0;
    const double y = bed.min.y() + margin + double(row) * cell_y + bbox_size.y() / 2.0;
    const double z = bed.min.z();   // sit on the bed

    return Vec3d(x, y, z);
}

} // namespace orca_cli
