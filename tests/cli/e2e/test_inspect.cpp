#include <catch2/catch_all.hpp>
#include "../test_common.hpp"

#include <boost/filesystem.hpp>
#include <string>

using namespace orca_cli_test;

// --------------------------------------------------------------------------
// `inspect`
// --------------------------------------------------------------------------

TEST_CASE("orca-cli: inspect on the reference 3mf prints plate and filament counts",
          "[orca-cli][P7][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "inspect-ref");
    auto r = run_cli({"inspect", in.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_.find("plates:") != std::string::npos);
    REQUIRE(r.stdout_.find("filament slots:") != std::string::npos);
}

TEST_CASE("orca-cli: inspect --json emits structured output",
          "[orca-cli][P7][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "inspect-json");
    auto r = run_cli({"--json", "inspect", in.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_.find("\"plate_count\":") != std::string::npos);
    REQUIRE(r.stdout_.find("\"filament_count\":") != std::string::npos);
    REQUIRE(r.stdout_.find("\"plates\":") != std::string::npos);
    REQUIRE(r.stdout_.find("\"objects\":") != std::string::npos);
    REQUIRE(r.stdout_.find("\"project_changed\":") != std::string::npos);
}

TEST_CASE("orca-cli: inspect rejects --output with usage_error",
          "[orca-cli][P7][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "inspect-out");
    auto r = run_cli({"inspect", in.string(),
                      "--output", (tmp / "x.3mf").string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 1); // usage_error
}

TEST_CASE("orca-cli: inspect returns file_not_found on missing file",
          "[orca-cli][P7][e2e]")
{
    auto r = run_cli({"inspect", "C:\\does\\not\\exist.3mf"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 2); // file_not_found
}

TEST_CASE("orca-cli: inspect reflects plate add + object add + config set mutations",
          "[orca-cli][P7][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "inspect-mut");
    REQUIRE(run_cli({"plate",  "add", in.string(),
                     "--name", "Inspectable"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "Inspectable",
                     "--stl",   cube.string(),
                     "--name",  "ic"}).exit_code == 0);
    REQUIRE(run_cli({"config", "set", in.string(),
                     "--object", "ic",
                     "--key",    "wall_loops",
                     "--value",  "5"}).exit_code == 0);

    auto r = run_cli({"--json", "inspect", in.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_.find("Inspectable") != std::string::npos);
    REQUIRE(r.stdout_.find("\"name\":\"ic\"") != std::string::npos);
    REQUIRE(r.stdout_.find("wall_loops") != std::string::npos);
}
