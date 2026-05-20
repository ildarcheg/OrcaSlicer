#include <catch2/catch_all.hpp>
#include "commands/object_parse_vec3.hpp"

using namespace orca_cli::commands;

TEST_CASE("parse_vec3 reports component_count", "[orca-cli][cleanup][T6]") {
    SECTION("1 component") {
        auto r = parse_vec3("2");
        REQUIRE(r.has_value());
        REQUIRE(r->component_count == 1);
        REQUIRE(r->values.x() == 2.0);
        REQUIRE(r->values.y() == 2.0);
        REQUIRE(r->values.z() == 2.0);
    }
    SECTION("2 components") {
        auto r = parse_vec3("60,60");
        REQUIRE(r.has_value());
        REQUIRE(r->component_count == 2);
        REQUIRE(r->values.x() == 60.0);
        REQUIRE(r->values.z() == 0.0);
    }
    SECTION("3 components") {
        auto r = parse_vec3("1,2,3");
        REQUIRE(r.has_value());
        REQUIRE(r->component_count == 3);
        REQUIRE(r->values.z() == 3.0);
    }
    SECTION("empty token rejected") {
        REQUIRE_FALSE(parse_vec3("60,,60").has_value());
    }
}
