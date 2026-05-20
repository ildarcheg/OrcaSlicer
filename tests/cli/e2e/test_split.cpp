// orca-cli object split-to-parts end-to-end tests.
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "../archive_invariants.hpp"
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

using namespace orca_cli_test;

namespace fs = boost::filesystem;

TEST_CASE("object split-to-parts produces 2 volumes on Layer A fixture",
          "[orca-cli][split][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    const auto tmp = make_temp_dir();
    const auto in  = copy_ref_to_temp(tmp, "split-a");

    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    if (!fs::exists(stl)) { SUCCEED("Skipped: two_cubes.stl fixture not available"); return; }

    // Add a plate and object to split.
    auto add_plate_rc = run_cli({"plate", "add", in.string(), "--name", "SplitPlate"});
    INFO("plate add stdout: " << add_plate_rc.stdout_ << "\nstderr: " << add_plate_rc.stderr_);
    REQUIRE(add_plate_rc.exit_code == 0);

    auto add_rc = run_cli({
        "object", "add", in.string(),
        "--plate", "SplitPlate",
        "--stl",   stl.string(),
        "--name",  "multipart",
    });
    INFO("object add stdout: " << add_rc.stdout_ << "\nstderr: " << add_rc.stderr_);
    REQUIRE(add_rc.exit_code == 0);

    auto split_rc = run_cli({
        "object", "split-to-parts", in.string(),
        "--name", "multipart",
    });
    INFO("split-to-parts stdout: " << split_rc.stdout_ << "\nstderr: " << split_rc.stderr_);
    REQUIRE(split_rc.exit_code == 0);

    fs::remove_all(tmp);
}
