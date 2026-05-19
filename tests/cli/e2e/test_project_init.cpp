// OrcaSlicer vendors Catch2 v3; umbrella header is catch_all.hpp.
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "../archive_invariants.hpp"

using namespace orca_cli_test;

TEST_CASE("orca-cli: project init clones reference and passes archive invariants",
          "[orca-cli][P1][e2e]") {
    if (ref_3mf().empty()) {
        SUCCEED("Skipped: reference 3mf not on host");
        return;
    }
    auto tmp = make_temp_dir();
    auto out = (tmp / "p1.3mf").string();

    auto r = run_cli({"project", "init", out, "--template", ref_3mf().string()});
    INFO("stdout: " << r.stdout_);
    INFO("stderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    archive::run_all_basic(out);
    archive::assert_printable_area_4_points(out);
    archive::assert_parts_have_source_file(out);
}

TEST_CASE("orca-cli: project init returns file_not_found on missing template",
          "[orca-cli][P1][e2e]") {
    auto tmp = make_temp_dir();
    auto out = (tmp / "x.3mf").string();
    auto r = run_cli({"project", "init", out, "--template",
                      "C:\\does\\not\\exist.3mf"});
    INFO("stdout: " << r.stdout_);
    INFO("stderr: " << r.stderr_);
    REQUIRE(r.exit_code == 2);
}
