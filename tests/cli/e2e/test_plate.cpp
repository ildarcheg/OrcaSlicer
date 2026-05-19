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
