#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "../archive_invariants.hpp"
#include "io.hpp"
#include "project_ops.hpp"

#include <boost/filesystem.hpp>
#include <string>

using namespace orca_cli_test;

TEST_CASE("orca-cli: plate add appends a new plate",
          "[orca-cli][P2][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "plate-add");
    auto r = run_cli({"plate", "add", in.string(), "--name", "Brackets"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    bool found = false;
    for (auto& p : s.plates)
        if (p->plate_name == "Brackets") found = true;
    REQUIRE(found);

    archive::run_all_basic(in);
}

TEST_CASE("orca-cli: plate add returns duplicate_name on collision",
          "[orca-cli][P2][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "plate-add-dup");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "Same"}).exit_code == 0);
    auto r = run_cli({"plate", "add", in.string(), "--name", "Same"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 5); // duplicate_name
}

TEST_CASE("orca-cli: plate rename happy path + duplicate-to + unknown-from",
          "[orca-cli][P2][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "plate-rename");
    REQUIRE(run_cli({"plate","add",in.string(),"--name","A"}).exit_code == 0);
    REQUIRE(run_cli({"plate","add",in.string(),"--name","B"}).exit_code == 0);

    auto ok = run_cli({"plate","rename",in.string(),"--from","A","--to","A2"});
    INFO("rename ok stdout: " << ok.stdout_ << " stderr: " << ok.stderr_);
    REQUIRE(ok.exit_code == 0);

    auto dup = run_cli({"plate","rename",in.string(),"--from","A2","--to","B"});
    INFO("rename dup stdout: " << dup.stdout_ << " stderr: " << dup.stderr_);
    REQUIRE(dup.exit_code == 5); // duplicate_name

    auto miss = run_cli({"plate","rename",in.string(),"--from","missing","--to","X"});
    INFO("rename miss stdout: " << miss.stdout_ << " stderr: " << miss.stderr_);
    REQUIRE(miss.exit_code == 6); // unknown_reference
}

TEST_CASE("orca-cli: plate remove removes by name",
          "[orca-cli][P2][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "plate-remove");
    REQUIRE(run_cli({"plate","add",in.string(),"--name","ToRemove"}).exit_code == 0);

    auto r = run_cli({"plate","remove",in.string(),"--name","ToRemove"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    bool found = false;
    for (auto& p : s.plates)
        if (p->plate_name == "ToRemove") found = true;
    REQUIRE_FALSE(found);
}

TEST_CASE("orca-cli: plate list rejects --output with usage_error",
          "[orca-cli][P2][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "plate-list-out");
    auto r = run_cli({"plate","list",in.string(),"--output",(tmp/"x.3mf").string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 1);
}

TEST_CASE("orca-cli: plate list prints plate count",
          "[orca-cli][P2][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "plate-list");
    auto r = run_cli({"plate","list",in.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_.find("plate") != std::string::npos);
}
