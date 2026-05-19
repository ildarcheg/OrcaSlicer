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

TEST_CASE("orca-cli: load_project rebuilds plate->objects_and_instances from obj_inst_map (G2)",
          "[orca-cli][P1][roundtrip]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }

    auto s = orca_cli::load_project(ref_3mf().string());

    // The reference 3mf has at least one plate with at least one object.
    // After load_project's G2 rebuild, at least one plate must have a
    // non-empty objects_and_instances vector.
    bool any_plate_has_objects = false;
    for (auto& p : s.plates) {
        if (!p->objects_and_instances.empty()) {
            any_plate_has_objects = true;
            // Verify indices are in range.
            for (auto& [oi, ii] : p->objects_and_instances) {
                REQUIRE(oi >= 0);
                REQUIRE(oi < int(s.model->objects.size()));
                auto* obj = s.model->objects[oi];
                REQUIRE(ii >= 0);
                REQUIRE(ii < int(obj->instances.size()));
            }
        }
    }
    REQUIRE(any_plate_has_objects);
}
