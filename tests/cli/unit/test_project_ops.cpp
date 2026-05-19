#include <catch2/catch_all.hpp>
#include "project_ops.hpp"
#include "io.hpp"
#include "../test_common.hpp"

#include <boost/filesystem.hpp>

#include <stdexcept>

using namespace orca_cli;

TEST_CASE("orca-cli: add_plate appends plate with given name",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());

    add_plate(s, "Brackets");
    REQUIRE(s.plates.size() == 2u);
    REQUIRE(s.plates.back()->plate_name == "Brackets");
}

TEST_CASE("orca-cli: add_plate rejects duplicate name",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Existing";

    REQUIRE_THROWS_AS(add_plate(s, "Existing"), std::invalid_argument);
}

TEST_CASE("orca-cli: remove_plate removes by name and re-indexes",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    for (int i = 0; i < 3; ++i) {
        s.plates.push_back(std::make_unique<Slic3r::PlateData>());
        s.plates.back()->plate_name  = "P" + std::to_string(i);
        s.plates.back()->plate_index = i;
    }
    remove_plate(s, "P1");
    REQUIRE(s.plates.size() == 2u);
    REQUIRE(s.plates[0]->plate_name == "P0");
    REQUIRE(s.plates[1]->plate_name == "P2");
    REQUIRE(s.plates[1]->plate_index == 1); // re-indexed from 2 to 1
}

TEST_CASE("orca-cli: remove_plate rejects unknown name",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Only";
    // Need >=2 plates to reach the lookup branch; otherwise we hit the
    // invalid_state guard first.
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Other";

    REQUIRE_THROWS_AS(remove_plate(s, "Missing"), std::out_of_range);
}

TEST_CASE("orca-cli: remove_plate refuses to remove only plate",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Only";

    REQUIRE_THROWS_AS(remove_plate(s, "Only"), std::invalid_argument);
}

TEST_CASE("orca-cli: rename_plate updates name only, no re-index",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    for (int i = 0; i < 2; ++i) {
        s.plates.push_back(std::make_unique<Slic3r::PlateData>());
        s.plates.back()->plate_name  = "P" + std::to_string(i);
        s.plates.back()->plate_index = i;
    }
    rename_plate(s, "P0", "Renamed");
    REQUIRE(s.plates[0]->plate_name  == "Renamed");
    REQUIRE(s.plates[0]->plate_index == 0); // unchanged
    REQUIRE(s.plates[1]->plate_index == 1);
}

TEST_CASE("orca-cli: rename_plate rejects unknown from",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "A";

    REQUIRE_THROWS_AS(rename_plate(s, "Missing", "X"), std::out_of_range);
}

TEST_CASE("orca-cli: rename_plate rejects duplicate to",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    for (auto& n : {"A", "B"}) {
        s.plates.push_back(std::make_unique<Slic3r::PlateData>());
        s.plates.back()->plate_name = n;
    }
    REQUIRE_THROWS_AS(rename_plate(s, "A", "B"), std::invalid_argument);
}

TEST_CASE("orca-cli: rename_plate self-rename succeeds if from exists", "[orca-cli][P2][unit]") {
    using namespace orca_cli;
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Same";
    REQUIRE_NOTHROW(rename_plate(s, "Same", "Same"));
}

TEST_CASE("orca-cli: rename_plate self-rename rejects unknown from", "[orca-cli][P2][unit]") {
    using namespace orca_cli;
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Exists";
    REQUIRE_THROWS_AS(rename_plate(s, "Missing", "Missing"), std::out_of_range);
}

// -- P3: add_object / remove_object ----------------------------------------

TEST_CASE("orca-cli: add_object loads STL, stamps source attribution, places on plate",
          "[orca-cli][P3][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "P3Test");

    const size_t objects_before = s.model->objects.size();

    AddObjectParams p;
    p.plate_name  = "P3Test";
    p.stl_path    = stl.string();
    p.object_name = "cube";
    p.count       = 1;
    add_object(s, p);

    REQUIRE(s.model->objects.size() == objects_before + 1u);
    auto* obj = s.model->objects.back();
    REQUIRE(obj->name == "cube");
    REQUIRE_FALSE(obj->volumes.empty());
    // Bug C defense: every volume's Source must be stamped.
    REQUIRE(obj->volumes.front()->source.input_file == stl.string());
    REQUIRE(obj->volumes.front()->source.object_idx == 0);
    REQUIRE(obj->volumes.front()->source.volume_idx == 0);
    REQUIRE(obj->instances.size() == 1u);

    // The new plate must reference at least one instance after the add.
    bool found_on_plate = false;
    for (auto& pd : s.plates) {
        if (pd->plate_name != "P3Test") continue;
        if (!pd->objects_and_instances.empty()) found_on_plate = true;
    }
    REQUIRE(found_on_plate);
}

TEST_CASE("orca-cli: add_object with count=3 creates 3 instances",
          "[orca-cli][P3][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "P3Count");

    AddObjectParams p;
    p.plate_name = "P3Count";
    p.stl_path   = stl.string();
    p.count      = 3;
    add_object(s, p);

    auto* obj = s.model->objects.back();
    REQUIRE(obj->instances.size() == 3u);
}

TEST_CASE("orca-cli: add_object rejects unknown plate",
          "[orca-cli][P3][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());

    AddObjectParams p;
    p.plate_name = "DoesNotExist";
    p.stl_path   = stl.string();
    REQUIRE_THROWS_AS(add_object(s, p), std::out_of_range);
}

TEST_CASE("orca-cli: remove_object removes by name and rebuilds plate map",
          "[orca-cli][P3][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "P3Rm");
    AddObjectParams p;
    p.plate_name  = "P3Rm";
    p.stl_path    = stl.string();
    p.object_name = "victim";
    add_object(s, p);
    const size_t n_before = s.model->objects.size();

    remove_object(s, "victim");
    REQUIRE(s.model->objects.size() == n_before - 1u);

    // Map must no longer reference the removed object.
    bool referenced = false;
    for (auto& pd : s.plates) {
        for (const auto& kv : pd->objects_and_instances) {
            if (kv.first < 0 || kv.first >= int(s.model->objects.size())) {
                referenced = true; // dangling -> bug
            }
        }
    }
    REQUIRE_FALSE(referenced);
}

TEST_CASE("orca-cli: remove_object rejects unknown name",
          "[orca-cli][P3][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    REQUIRE_THROWS_AS(remove_object(s, "ghost-does-not-exist"),
                      std::out_of_range);
}
