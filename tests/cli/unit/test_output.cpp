#include <catch2/catch_all.hpp>
#include "output.hpp"

using namespace orca_cli;

TEST_CASE("orca-cli: escape_json escapes basic JSON metacharacters", "[orca-cli][P2][unit]") {
    REQUIRE(escape_json("plain")              == "plain");
    REQUIRE(escape_json("with\"quote")        == "with\\\"quote");
    REQUIRE(escape_json("with\\backslash")    == "with\\\\backslash");
    REQUIRE(escape_json("line1\nline2")       == "line1\\nline2");
    REQUIRE(escape_json("tab\there")          == "tab\\there");
}

TEST_CASE("orca-cli: escape_json escapes low control bytes as unicode", "[orca-cli][P2][unit]") {
    // \x01 -> 
    std::string in;  in.push_back('\x01');
    REQUIRE(escape_json(in) == "\\u0001");
    // \x1f -> 
    std::string in2; in2.push_back('\x1f');
    REQUIRE(escape_json(in2) == "\\u001f");
}
