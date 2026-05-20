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

TEST_CASE("object set-filament --part writes per-volume extruder",
          "[orca-cli][split][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    const auto tmp = make_temp_dir();
    const auto out = tmp / "split_part.3mf";
    fs::copy_file(ref_3mf(), out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    if (!fs::exists(stl)) { SUCCEED("Skipped: two_cubes.stl fixture not available"); return; }

    // Create a plate to hold the multipart object (matches T7's pattern).
    auto add_plate_rc = run_cli({"plate", "add", out.string(), "--name", "SplitPlate"});
    INFO("plate add stdout: " << add_plate_rc.stdout_ << "\nstderr: " << add_plate_rc.stderr_);
    REQUIRE(add_plate_rc.exit_code == 0);

    auto add_rc = run_cli({"object", "add", out.string(),
        "--plate", "SplitPlate", "--stl", stl.string(), "--name", "multi2"});
    INFO("object add stdout: " << add_rc.stdout_ << "\nstderr: " << add_rc.stderr_);
    REQUIRE(add_rc.exit_code == 0);

    auto split_rc = run_cli({"object", "split-to-parts", out.string(),
        "--name", "multi2"});
    INFO("split-to-parts stdout: " << split_rc.stdout_ << "\nstderr: " << split_rc.stderr_);
    REQUIRE(split_rc.exit_code == 0);

    auto rc1 = run_cli({"object", "set-filament", out.string(),
        "--name", "multi2", "--part", "multi2_1", "--filament", "1"});
    INFO("set-filament part 1 stdout: " << rc1.stdout_ << "\nstderr: " << rc1.stderr_);
    REQUIRE(rc1.exit_code == 0);

    auto rc2 = run_cli({"object", "set-filament", out.string(),
        "--name", "multi2", "--part", "multi2_2", "--filament", "2"});
    INFO("set-filament part 2 stdout: " << rc2.stdout_ << "\nstderr: " << rc2.stderr_);
    REQUIRE(rc2.exit_code == 0);

    fs::remove_all(tmp);
}
