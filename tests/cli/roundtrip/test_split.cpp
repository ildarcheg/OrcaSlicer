// Roundtrip tests for orca-cli split-to-parts. Verify that volume count
// and per-volume extruder values survive a full save -> reopen cycle via
// libslic3r's bbs_3mf reader.
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "io.hpp"
#include "project_ops.hpp"
#include <boost/filesystem.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

namespace fs = boost::filesystem;
using namespace orca_cli;
using namespace orca_cli_test;

TEST_CASE("volume count survives save/load roundtrip",
          "[orca-cli][split][roundtrip]") {
    if (ref_3mf().empty()) {
        SUCCEED("Skipped: reference 3mf not on host");
        return;
    }

    auto tmp = make_temp_dir();
    auto out = copy_ref_to_temp(tmp, "rt-split-count");

    // Build the state in memory, split, save.
    {
        ProjectState s = load_project(out.string());
        AddObjectParams p;
        p.plate_name  = s.plates.front()->plate_name;
        p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
        p.object_name = "rt";
        p.count       = 1;
        REQUIRE_NOTHROW(add_object(s, p));
        REQUIRE_NOTHROW(split_object_to_parts(s, "rt"));
        REQUIRE_NOTHROW(save_project(s, out.string()));
    }

    // Reopen and assert volume count.
    ProjectState s2 = load_project(out.string());
    auto* obj = find_object(s2, "rt");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    REQUIRE(obj->volumes[0]->name == std::string("rt_1"));
    REQUIRE(obj->volumes[1]->name == std::string("rt_2"));

    fs::remove_all(tmp);
}

TEST_CASE("per-part extruder survives save/load roundtrip",
          "[orca-cli][split][roundtrip]") {
    if (ref_3mf().empty()) {
        SUCCEED("Skipped: reference 3mf not on host");
        return;
    }

    using namespace Slic3r;
    auto tmp = make_temp_dir();
    auto out = copy_ref_to_temp(tmp, "rt-split-extruder");

    {
        ProjectState s = load_project(out.string());
        AddObjectParams p;
        p.plate_name  = s.plates.front()->plate_name;
        p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
        p.object_name = "rtx";
        p.count       = 1;
        REQUIRE_NOTHROW(add_object(s, p));
        REQUIRE_NOTHROW(split_object_to_parts(s, "rtx"));
        REQUIRE_NOTHROW(set_object_filament(s, "rtx", 1,
                                            std::optional<std::string>("rtx_1")));
        REQUIRE_NOTHROW(set_object_filament(s, "rtx", 2,
                                            std::optional<std::string>("rtx_2")));
        REQUIRE_NOTHROW(save_project(s, out.string()));
    }

    ProjectState s2 = load_project(out.string());
    auto* obj = find_object(s2, "rtx");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    auto* e1 = obj->volumes[0]->config.get().opt<ConfigOptionInt>("extruder");
    auto* e2 = obj->volumes[1]->config.get().opt<ConfigOptionInt>("extruder");
    REQUIRE(e1 != nullptr);
    REQUIRE(e2 != nullptr);
    REQUIRE(e1->value == 1);
    REQUIRE(e2->value == 2);

    fs::remove_all(tmp);
}
