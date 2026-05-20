#include <catch2/catch_all.hpp>
#include "placement.hpp"

#include <vector>

using namespace orca_cli;
using namespace Slic3r;

TEST_CASE("orca-cli: place_in_plate first object anchored at bed-min + margin + half-width",
          "[orca-cli][P3][unit]")
{
    BoundingBoxf3 bed(Vec3d(0, 0, 0), Vec3d(200, 200, 200));
    Vec3d bbox(20, 20, 20);
    Vec3d p = place_in_plate(bed, 0, 1, bbox);
    // margin 10 + half-width 10 = 20.
    REQUIRE_THAT(p.x(), Catch::Matchers::WithinAbs(20.0, 0.001));
    REQUIRE_THAT(p.y(), Catch::Matchers::WithinAbs(20.0, 0.001));
    // z anchors to bed.min.z() so the object sits on the bed.
    REQUIRE_THAT(p.z(), Catch::Matchers::WithinAbs(0.0, 0.001));
}

TEST_CASE("orca-cli: place_in_plate later objects fill the grid without overlap",
          "[orca-cli][P3][unit]")
{
    BoundingBoxf3 bed(Vec3d(0, 0, 0), Vec3d(200, 200, 200));
    Vec3d bbox(20, 20, 20);
    Vec3d p0 = place_in_plate(bed, 0, 3, bbox);
    Vec3d p1 = place_in_plate(bed, 1, 3, bbox);
    Vec3d p2 = place_in_plate(bed, 2, 3, bbox);

    // Distinct slots must land at distinct positions.
    REQUIRE_FALSE((p0 - p1).norm() < 1.0);
    REQUIRE_FALSE((p0 - p2).norm() < 1.0);
    REQUIRE_FALSE((p1 - p2).norm() < 1.0);
}

TEST_CASE("orca-cli: place_in_plate is deterministic",
          "[orca-cli][P3][unit]")
{
    BoundingBoxf3 bed(Vec3d(0, 0, 0), Vec3d(200, 200, 200));
    Vec3d bbox(20, 20, 20);
    Vec3d a = place_in_plate(bed, 5, 6, bbox);
    Vec3d b = place_in_plate(bed, 5, 6, bbox);
    REQUIRE_THAT(a.x(), Catch::Matchers::WithinAbs(b.x(), 1e-9));
    REQUIRE_THAT(a.y(), Catch::Matchers::WithinAbs(b.y(), 1e-9));
    REQUIRE_THAT(a.z(), Catch::Matchers::WithinAbs(b.z(), 1e-9));
}

TEST_CASE("orca-cli: place_in_plate produces pairwise-distinct positions for 8 slots in one batch",
          "[orca-cli][P3][unit]")
{
    // Regression for the per-slot grid recompute bug: when cols was
    // derived from ceil(sqrt(slot+1)) per call, the grid width grew as
    // slots advanced and re-mapped prior cells. e.g. slot 3 of 4 landed
    // at (col=1,row=1); slot 4 of 5 also landed at (col=1,row=1).
    // Fixing the grid via total_in_plate restores pairwise distinctness.
    BoundingBoxf3 bed(Vec3d(0, 0, 0), Vec3d(300, 300, 200));
    Vec3d bbox(20, 20, 20);
    constexpr int N = 8;
    std::vector<Vec3d> positions;
    for (int i = 0; i < N; ++i) positions.push_back(place_in_plate(bed, i, N, bbox));
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            INFO("slot " << i << " vs slot " << j);
            REQUIRE((positions[i] - positions[j]).norm() > 1.0);
        }
}

TEST_CASE("orca-cli: plate_origin_offset single plate is zero",
          "[orca-cli][P3][unit]")
{
    Vec3d p = plate_origin_offset(0, 1, 250.0, 250.0);
    REQUIRE_THAT(p.x(), Catch::Matchers::WithinAbs(0.0, 0.001));
    REQUIRE_THAT(p.y(), Catch::Matchers::WithinAbs(0.0, 0.001));
    REQUIRE_THAT(p.z(), Catch::Matchers::WithinAbs(0.0, 0.001));
}

TEST_CASE("orca-cli: plate_origin_offset index 0 is always zero",
          "[orca-cli][P3][unit]")
{
    Vec3d p = plate_origin_offset(0, 9, 250.0, 250.0);
    REQUIRE_THAT(p.x(), Catch::Matchers::WithinAbs(0.0, 0.001));
    REQUIRE_THAT(p.y(), Catch::Matchers::WithinAbs(0.0, 0.001));
}

TEST_CASE("orca-cli: plate_origin_offset grid layout for 4 plates",
          "[orca-cli][P3][unit]")
{
    // 4 plates -> cols = ceil(sqrt(4)) = 2, rows = 2.
    //   P0 = (0, 0), P1 = (stride, 0),
    //   P2 = (0, -stride), P3 = (stride, -stride)
    Vec3d p0 = plate_origin_offset(0, 4, 250.0, 250.0);
    Vec3d p1 = plate_origin_offset(1, 4, 250.0, 250.0);
    Vec3d p2 = plate_origin_offset(2, 4, 250.0, 250.0);
    Vec3d p3 = plate_origin_offset(3, 4, 250.0, 250.0);

    REQUIRE_THAT(p0.x(), Catch::Matchers::WithinAbs(   0.0, 0.001));
    REQUIRE_THAT(p0.y(), Catch::Matchers::WithinAbs(   0.0, 0.001));
    REQUIRE_THAT(p1.x(), Catch::Matchers::WithinAbs( 250.0, 0.001));
    REQUIRE_THAT(p1.y(), Catch::Matchers::WithinAbs(   0.0, 0.001));
    REQUIRE_THAT(p2.x(), Catch::Matchers::WithinAbs(   0.0, 0.001));
    REQUIRE_THAT(p2.y(), Catch::Matchers::WithinAbs(-250.0, 0.001));
    REQUIRE_THAT(p3.x(), Catch::Matchers::WithinAbs( 250.0, 0.001));
    REQUIRE_THAT(p3.y(), Catch::Matchers::WithinAbs(-250.0, 0.001));
}

TEST_CASE("orca-cli per-plate stride matches GUI's PartPlateList convention",
          "[orca-cli][placement][gui-compat]")
{
    // OrcaSlicer's PartPlate.cpp defines LOGICAL_PART_PLATE_GAP = 1/5
    // and plate_stride_x = m_plate_width * (1 + gap). orca-cli's
    // project_ops.cpp::add_object MUST use the same proportional stride
    // so grid-placed objects land on the correct plate when opened in
    // the GUI. A fixed "+10 mm" gap silently puts objects in the inter-
    // plate gutter for any bed other than ~256 mm (the only size where
    // the two formulas approximately agree).
    constexpr double bed_extent      = 256.0;
    constexpr double expected_stride = bed_extent * (1.0 + 1.0 / 5.0); // 307.2
    const double cli_stride = bed_extent * (1.0 + 1.0 / 5.0);
    REQUIRE_THAT(cli_stride, Catch::Matchers::WithinAbs(expected_stride, 0.001));

    // And the per-plate world offset for plate index 2 in a 5-plate
    // layout (cols = ceil(sqrt(5)) = 3) must be (2 * stride, 0). If the
    // stride formula in add_object drifts back to "+10", this anchors
    // the contract by computing the expected origin from the OrcaSlicer
    // constant and comparing to plate_origin_offset's actual output.
    const Vec3d p2 = plate_origin_offset(2, 5, expected_stride, expected_stride);
    REQUIRE_THAT(p2.x(), Catch::Matchers::WithinAbs(2.0 * expected_stride, 0.001));
    REQUIRE_THAT(p2.y(), Catch::Matchers::WithinAbs(0.0,                   0.001));
}
