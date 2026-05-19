// OrcaSlicer vendors Catch2 v3; umbrella header is catch_all.hpp.
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"

using namespace orca_cli_test;

TEST_CASE("orca-cli: --version exits 0 and prints a version line", "[orca-cli][P0][e2e]") {
    auto r = run_cli({"--version"});
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_.find("orca-cli") != std::string::npos);
}

TEST_CASE("orca-cli: bare invocation (no subcommand) returns usage_error", "[orca-cli][P0][e2e]") {
    auto r = run_cli({});
    REQUIRE(r.exit_code == 1);
}
