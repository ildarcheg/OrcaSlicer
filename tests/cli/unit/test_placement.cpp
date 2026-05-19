#include <catch2/catch_all.hpp>
#include "placement.hpp"

using namespace orca_cli;
using namespace Slic3r;

TEST_CASE("orca-cli: place_in_plate first object anchored at bed-min + margin + half-width",
          "[orca-cli][P3][unit]")
{
    BoundingBoxf3 bed(Vec3d(0, 0, 0), Vec3d(200, 200, 200));
    Vec3d bbox(20, 20, 20);
    Vec3d p = place_in_plate(bed, 0, bbox);
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
    Vec3d p0 = place_in_plate(bed, 0, bbox);
    Vec3d p1 = place_in_plate(bed, 1, bbox);
    Vec3d p2 = place_in_plate(bed, 2, bbox);

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
    Vec3d a = place_in_plate(bed, 5, bbox);
    Vec3d b = place_in_plate(bed, 5, bbox);
    REQUIRE_THAT(a.x(), Catch::Matchers::WithinAbs(b.x(), 1e-9));
    REQUIRE_THAT(a.y(), Catch::Matchers::WithinAbs(b.y(), 1e-9));
    REQUIRE_THAT(a.z(), Catch::Matchers::WithinAbs(b.z(), 1e-9));
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
