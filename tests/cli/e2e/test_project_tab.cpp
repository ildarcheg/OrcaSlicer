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

TEST_CASE("orca-cli: project profile set + show --json round-trips fields",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "profile-set");
    REQUIRE(run_cli({"project", "profile", "set", in.string(),
                     "--title", "PT", "--description", "PD"}).exit_code == 0);

    auto r = run_cli({"--json", "project", "profile", "show", in.string()});
    INFO("show stdout: " << r.stdout_);
    REQUIRE(r.exit_code == 0);
    auto j = parse_json_envelope(r.stdout_);
    REQUIRE(j["data"]["title"]       == "PT");
    REQUIRE(j["data"]["description"] == "PD");
    REQUIRE(j["data"].contains("user_id"));    // read-only, always present
    REQUIRE(j["data"].contains("user_name"));
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

TEST_CASE("orca-cli: project aux add then list reports under correct bucket",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-add");

    REQUIRE(run_cli({"project", "aux", "add", in.string(),
                     "--folder", "others",
                     "--file",   cube.string(),
                     "--name",   "sample.stl"}).exit_code == 0);

    auto r = run_cli({"--json", "project", "aux", "list", in.string()});
    INFO("list stdout: " << r.stdout_);
    REQUIRE(r.exit_code == 0);
    auto j = parse_json_envelope(r.stdout_);
    // Stable shape: every bucket key present (spec § 2.2)
    REQUIRE(j["data"].contains("pictures"));
    REQUIRE(j["data"].contains("bom"));
    REQUIRE(j["data"].contains("assembly_guide"));  // underscore — spec § 2.3
    REQUIRE(j["data"].contains("others"));
    bool saw = false;
    for (const auto& e : j["data"]["others"])
        if (e["name"] == "sample.stl") saw = true;
    REQUIRE(saw);
}

TEST_CASE("orca-cli: project aux add --folder uses hyphen (assembly-guide)",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-assembly");

    REQUIRE(run_cli({"project", "aux", "add", in.string(),
                     "--folder", "assembly-guide",   // hyphen on flag
                     "--file",   cube.string(),
                     "--name",   "instructions.bin"}).exit_code == 0);

    auto r = run_cli({"--json", "project", "aux", "list", in.string()});
    auto j = parse_json_envelope(r.stdout_);
    bool saw = false;
    for (const auto& e : j["data"]["assembly_guide"])    // underscore in JSON
        if (e["name"] == "instructions.bin") saw = true;
    REQUIRE(saw);
}

TEST_CASE("orca-cli: project aux add collision is exit 5; --force is exit 0",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-collide");
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","x.bin"}).exit_code == 0);
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","x.bin"}).exit_code
            == int(orca_cli::ExitCode::duplicate_name));
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","x.bin","--force"}).exit_code == 0);
}

TEST_CASE("orca-cli: project aux add --name CON.png is exit 4 (bad_config)",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-cn");
    auto r = run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","CON.png"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == int(orca_cli::ExitCode::bad_config));
}

TEST_CASE("orca-cli: project aux remove missing-name is exit 6",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-rm-missing");
    auto r = run_cli({"project","aux","remove",in.string(),"--folder","pictures","--name","never_there.png"});
    REQUIRE(r.exit_code == int(orca_cli::ExitCode::unknown_reference));
}

TEST_CASE("orca-cli: project aux export to file and to directory destinations",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-export");
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","x.bin"}).exit_code == 0);

    auto out_dir = make_temp_dir();
    auto file_dst = out_dir / "renamed.bin";
    REQUIRE(run_cli({"project","aux","export",in.string(),"--folder","others","--name","x.bin","--to",file_dst.string()}).exit_code == 0);
    REQUIRE(fs::exists(file_dst));

    auto dir_dst = make_temp_dir();
    REQUIRE(run_cli({"project","aux","export",in.string(),"--folder","others","--name","x.bin","--to",dir_dst.string()}).exit_code == 0);
    REQUIRE(fs::exists(dir_dst / "x.bin"));
}

TEST_CASE("orca-cli: project aux export to non-existent --to parent is exit 4",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "aux-export-bad");
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","others","--file",cube.string(),"--name","x.bin"}).exit_code == 0);
    auto bad = make_temp_dir() / "no_such_dir" / "out.bin";
    REQUIRE(run_cli({"project","aux","export",in.string(),"--folder","others","--name","x.bin","--to",bad.string()}).exit_code
            == int(orca_cli::ExitCode::bad_config));
}

TEST_CASE("orca-cli: project info clear --field tolerates whitespace after commas",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-clear-ws");
    REQUIRE(run_cli({"project","info","set",in.string(),
                     "--title","T","--description","D"}).exit_code == 0);
    // Whitespace after each comma; split_csv should trim before matching the
    // whitelist (regression for the trim-the-comment-but-not-the-code bug).
    auto r = run_cli({"project","info","clear",in.string(),
                      "--field","title, description"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    auto r2 = run_cli({"--json","project","info","show",in.string()});
    auto j = parse_json_envelope(r2.stdout_);
    REQUIRE(j["data"]["title"]       == "");
    REQUIRE(j["data"]["description"] == "");
}

TEST_CASE("orca-cli: project info show human-readable emits (empty) for unset fields",
          "[orca-cli][project-tab][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "info-show-empty");
    // Set title to a non-empty value, leave others unset (or already empty).
    REQUIRE(run_cli({"project","info","set",in.string(),"--title","Only"}).exit_code == 0);
    REQUIRE(run_cli({"project","info","clear",in.string(),
                     "--field","description,license,copyright,cover"}).exit_code == 0);
    auto r = run_cli({"project","info","show",in.string()}); // no --json
    INFO("stdout: " << r.stdout_);
    REQUIRE(r.exit_code == 0);
    // Spec § 2.2: human-readable mode shows "(empty)" for cleared fields.
    REQUIRE(r.stdout_.find("title:       Only")      != std::string::npos);
    REQUIRE(r.stdout_.find("description: (empty)")   != std::string::npos);
    REQUIRE(r.stdout_.find("license:     (empty)")   != std::string::npos);
    REQUIRE(r.stdout_.find("copyright:   (empty)")   != std::string::npos);
    REQUIRE(r.stdout_.find("cover:       (empty)")   != std::string::npos);
}
