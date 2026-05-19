#include <catch2/catch_all.hpp>
#include "project_ops.hpp"

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
