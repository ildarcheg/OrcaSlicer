// OrcaSlicer vendors Catch2 v3; umbrella header is catch_all.hpp.
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "io.hpp"

using namespace orca_cli;
using namespace orca_cli_test;

TEST_CASE("orca-cli: load_project loads model and config from reference 3mf",
          "[orca-cli][P1][roundtrip]") {
    if (ref_3mf().empty()) {
        SUCCEED("Skipped: reference 3mf not on host");
        return;
    }
    auto state = load_project(ref_3mf().string());
    REQUIRE(state.model          != nullptr);
    REQUIRE(state.project_config != nullptr);
    REQUIRE(!state.plates.empty());
    REQUIRE(state.model->objects.size() > 0);
}
