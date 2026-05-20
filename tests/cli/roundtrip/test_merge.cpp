// Roundtrip test for orca-cli merge-parts. Verifies that the merged
// volume's name, extruder, and per-volume config survive a full
// save -> reopen cycle via libslic3r's bbs_3mf reader.
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "io.hpp"
#include "project_ops.hpp"
#include <boost/filesystem.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

namespace fs = boost::filesystem;

TEST_CASE("merge-parts: merged volume name + extruder + per-vol config "
          "survive save/load roundtrip",
          "[orca-cli][merge][roundtrip]") {
    using namespace orca_cli;
    using namespace Slic3r;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = orca_cli_test::copy_ref_to_temp(tmp, "merge-rt");

    {
        ProjectState s = load_project(out.string());
        AddObjectParams p;
        p.plate_name  = s.plates.front()->plate_name;
        p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
        p.object_name = "rt_merge";
        p.count       = 1;
        REQUIRE_NOTHROW(add_object(s, p));
        REQUIRE_NOTHROW(split_object_to_parts(s, "rt_merge"));
        // Stamp identical filament slot 2 on both parts so merge inherits
        // without requiring a --filament override.
        REQUIRE_NOTHROW(set_object_filament(s, "rt_merge", 2,
            std::optional<std::string>("rt_merge_1")));
        REQUIRE_NOTHROW(set_object_filament(s, "rt_merge", 2,
            std::optional<std::string>("rt_merge_2")));
        // Stamp identical wall_loops=4 on both parts so merge inherits.
        auto* obj_pre = find_object(s, "rt_merge");
        REQUIRE(obj_pre != nullptr);
        REQUIRE(obj_pre->volumes.size() == 2);
        obj_pre->volumes[0]->config.set("wall_loops", 4);
        obj_pre->volumes[1]->config.set("wall_loops", 4);

        REQUIRE_NOTHROW(merge_object_parts(s, "rt_merge",
            {"rt_merge_1", "rt_merge_2"},
            "rt_merge_main", std::nullopt));
        REQUIRE_NOTHROW(save_project(s, out.string()));
    }

    // Reopen and assert name, extruder, and per-vol config.
    //
    // Note: bbs_3mf unconditionally erases `extruder` from the volume-level
    // config on load when the object has only one volume (bbs_3mf.cpp ~2236).
    // For single-volume objects the effective extruder lives on the object
    // config, not the volume config. So after merge → save → reload we check
    // obj->config for extruder and obj->volumes[0]->config for wall_loops.
    ProjectState s2 = load_project(out.string());
    auto* obj = find_object(s2, "rt_merge");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(obj->volumes[0]->name == std::string("rt_merge_main"));
    // Extruder is on the object config for single-volume objects after reload.
    auto* ext = obj->config.get().opt<ConfigOptionInt>("extruder");
    REQUIRE(ext != nullptr);
    REQUIRE(ext->value == 2);
    // Per-volume non-extruder config (wall_loops) survives on volume config.
    auto* wl = obj->volumes[0]->config.get().opt<ConfigOptionInt>("wall_loops");
    REQUIRE(wl != nullptr);
    REQUIRE(wl->value == 4);

    fs::remove_all(tmp);
}
