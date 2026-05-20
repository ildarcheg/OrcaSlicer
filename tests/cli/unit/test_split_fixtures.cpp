// Fixture sanity tests for orca-cli split-to-parts. These run first and
// gate every downstream test in the [orca-cli][split] family.
//
// Layer A: tests/cli/fixtures/two_cubes.stl. Committed in-tree, always
// present. Must contain exactly 2 connected mesh components.
//
// Layer B: $ORCA_CLI_STL_DIR/box_with_text.stl. Optional, local-dev
// only. Test SKIPs when absent. (Added in T3.)
#include <catch2/catch_all.hpp>
#include <boost/filesystem.hpp>
#include <libslic3r/Format/STL.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleMesh.hpp>

namespace fs = boost::filesystem;

TEST_CASE("two_cubes.stl is two-component (Layer A fixture sanity)",
          "[orca-cli][split][fixture]") {
    const auto path = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    REQUIRE(fs::exists(path)); // hard-required: committed in-tree
    Slic3r::Model m;
    REQUIRE(Slic3r::load_stl(path.string().c_str(), &m, nullptr));
    REQUIRE(m.objects.size() == 1);
    REQUIRE(m.objects[0]->volumes.size() == 1);
    auto components = m.objects[0]->volumes[0]->mesh().split();
    INFO("connected components: " << components.size());
    REQUIRE(components.size() == 2);
}
