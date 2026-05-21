// tests/cli/e2e/test_project_tab.cpp
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "io.hpp"
#include "output.hpp"
#include "project_ops.hpp"
#include "project_tab_ops.hpp"

#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>

#include <fstream>
#include <iterator>
#include <vector>

using namespace orca_cli_test;
namespace fs = boost::filesystem;

// Helper: parse the JSON envelope emitted by --json mode.
static nlohmann::json parse_json_envelope(const std::string& out) {
    auto idx = out.find('{');
    REQUIRE(idx != std::string::npos);
    return nlohmann::json::parse(out.substr(idx));
}

TEST_CASE("orca-cli: project info set + show --json round-trips fields",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-set");

    auto r = run_cli({"project", "info", "set", in.string(),
                      "--title", "Smoke",
                      "--description", "auto-set",
                      "--license", "MIT"});
    INFO("set stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto r2 = run_cli({"--json", "project", "info", "show", in.string()});
    INFO("show stdout: " << r2.stdout_ << "\nstderr: " << r2.stderr_);
    REQUIRE(r2.exit_code == 0);
    auto j = parse_json_envelope(r2.stdout_);
    REQUIRE(j["data"]["title"]       == "Smoke");
    REQUIRE(j["data"]["description"] == "auto-set");
    REQUIRE(j["data"]["license"]     == "MIT");
}

TEST_CASE("orca-cli: project info set with zero field flags is usage error",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-set-zero");
    auto r = run_cli({"project", "info", "set", in.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == int(orca_cli::ExitCode::usage_error));
}

TEST_CASE("orca-cli: project info show --json on pristine 3mf emits stable shape",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-show-pristine");
    auto r = run_cli({"--json", "project", "info", "show", in.string()});
    REQUIRE(r.exit_code == 0);
    auto j = parse_json_envelope(r.stdout_);
    for (const auto* k : {"title","description","license","copyright","cover","origin"})
        REQUIRE(j["data"].contains(k));
}

TEST_CASE("orca-cli: project info clear nulls named fields",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-clear");
    REQUIRE(run_cli({"project", "info", "set", in.string(),
                     "--title", "T", "--description", "D"}).exit_code == 0);
    REQUIRE(run_cli({"project", "info", "clear", in.string(),
                     "--field", "title,description"}).exit_code == 0);
    auto r = run_cli({"--json", "project", "info", "show", in.string()});
    auto j = parse_json_envelope(r.stdout_);
    REQUIRE(j["data"]["title"]       == "");
    REQUIRE(j["data"]["description"] == "");
}

TEST_CASE("orca-cli: project info set --output O writes to O and leaves input untouched",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-output");
    auto out = tmp / "copy.3mf";

    // Snapshot the input bytes so we can verify it wasn't touched.
    std::ifstream sf(in.string(), std::ios::binary);
    std::vector<unsigned char> input_before(
        std::istreambuf_iterator<char>(sf), std::istreambuf_iterator<char>{});

    auto r = run_cli({"project", "info", "set", in.string(),
                      "--title", "OutputSmoke",
                      "--output", out.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    // (a) --output path exists and carries the mutation.
    REQUIRE(fs::exists(out));
    auto r2 = run_cli({"--json", "project", "info", "show", out.string()});
    REQUIRE(r2.exit_code == 0);
    auto j = parse_json_envelope(r2.stdout_);
    REQUIRE(j["data"]["title"] == "OutputSmoke");

    // (b) input file is byte-equal to before.
    std::ifstream af(in.string(), std::ios::binary);
    std::vector<unsigned char> input_after(
        std::istreambuf_iterator<char>(af), std::istreambuf_iterator<char>{});
    REQUIRE(input_before == input_after);
}
