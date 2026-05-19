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
