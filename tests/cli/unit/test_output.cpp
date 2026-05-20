#include <catch2/catch_all.hpp>
#include "output.hpp"
#include <nlohmann/json.hpp>

using namespace orca_cli;

TEST_CASE("print_ok JSON envelope is parseable and well-formed", "[orca-cli][cleanup][T9]") {
    nlohmann::json data;
    data["plates"] = nlohmann::json::array();
    data["plates"].push_back({{"index", 1}, {"name", "P\"weird\""}});
    std::string body = build_ok_envelope("listed 1 plates", data);
    auto j = nlohmann::json::parse(body);
    REQUIRE(j["status"]  == "ok");
    REQUIRE(j["code"]    == "ok");
    REQUIRE(j["message"] == "listed 1 plates");
    REQUIRE(j["data"]["plates"][0]["name"] == "P\"weird\"");
}

TEST_CASE("build_err_envelope encodes code name", "[orca-cli][cleanup][T9]") {
    std::string body = build_err_envelope(ExitCode::bad_config, "key x");
    auto j = nlohmann::json::parse(body);
    REQUIRE(j["status"] == "err");
    REQUIRE(j["code"]   == "bad_config");
    REQUIRE(j["message"] == "key x");
}
