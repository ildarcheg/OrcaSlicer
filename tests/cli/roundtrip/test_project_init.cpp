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

TEST_CASE("orca-cli: save_project round-trips the reference 3mf",
          "[orca-cli][P1][roundtrip]") {
    if (ref_3mf().empty()) {
        SUCCEED("Skipped: reference 3mf not on host");
        return;
    }
    auto tmp = make_temp_dir();
    auto out = (tmp / "roundtrip.3mf").string();
    auto s_in = load_project(ref_3mf().string());
    save_project(s_in, out);
    auto s_out = load_project(out);
    REQUIRE(s_out.plates.size()         == s_in.plates.size());
    REQUIRE(s_out.model->objects.size() == s_in.model->objects.size());
}
