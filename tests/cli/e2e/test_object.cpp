#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "../archive_invariants.hpp"
#include "io.hpp"
#include "project_ops.hpp"

#include <boost/filesystem.hpp>
#include <string>

using namespace orca_cli_test;

TEST_CASE("orca-cli: object add places STL on named plate with source_file stamped",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-add");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "Brackets"}).exit_code == 0);

    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "Brackets",
                      "--stl",   cube.string(),
                      "--name",  "testcube"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    archive::run_all_basic(in);
    archive::assert_parts_have_source_file(in);

    // Re-load and confirm the object exists.
    auto s = orca_cli::load_project(in.string());
    bool found = false;
    for (auto* obj : s.model->objects)
        if (obj->name == "testcube") found = true;
    REQUIRE(found);
}

TEST_CASE("orca-cli: object add --count 3 creates 3 instances",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-add-count");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "P"}).exit_code == 0);
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "P",
                      "--stl",   cube.string(),
                      "--count", "3",
                      "--name",  "triple"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    int total_instances = 0;
    for (auto* obj : s.model->objects)
        if (obj->name == "triple") total_instances = int(obj->instances.size());
    REQUIRE(total_instances == 3);
}

TEST_CASE("orca-cli: object add returns unknown_reference on missing plate",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-add-noplate");
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "Nope",
                      "--stl",   cube.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 6); // unknown_reference
}

TEST_CASE("orca-cli: object add returns file_not_found on missing stl",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-add-nostl");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "X"}).exit_code == 0);
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "X",
                      "--stl",   "C:\\does\\not\\exist.stl"});
    REQUIRE(r.exit_code == 2); // file_not_found
}

TEST_CASE("orca-cli: object remove removes by name",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-remove");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "R"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "R",
                     "--stl",   cube.string(),
                     "--name",  "gone"}).exit_code == 0);
    auto r = run_cli({"object", "remove", in.string(), "--name", "gone"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    bool found = false;
    for (auto* obj : s.model->objects)
        if (obj->name == "gone") found = true;
    REQUIRE_FALSE(found);
}

TEST_CASE("orca-cli: object remove returns unknown_reference on missing name",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-rm-missing");
    auto r = run_cli({"object", "remove", in.string(), "--name", "ghost"});
    REQUIRE(r.exit_code == 6);
}

TEST_CASE("orca-cli: object list rejects --output with usage_error",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-list-out");
    auto r = run_cli({"object", "list", in.string(),
                      "--output", (tmp / "x.3mf").string()});
    REQUIRE(r.exit_code == 1);
}

TEST_CASE("orca-cli: object list lists objects",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-list");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "L"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "L",
                     "--stl",   cube.string(),
                     "--name",  "listed"}).exit_code == 0);
    auto r = run_cli({"object", "list", in.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_.find("listed") != std::string::npos);
}
