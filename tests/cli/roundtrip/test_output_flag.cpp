#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "io.hpp"
#include "project_ops.hpp"

#include <boost/filesystem.hpp>

using namespace orca_cli;
using namespace orca_cli_test;

TEST_CASE("orca-cli: --output leaves input byte-identical, writes side-car",
          "[orca-cli][P2][roundtrip]")
{
    if (ref_3mf().empty()) {
        SUCCEED("Skipped: reference 3mf not available");
        return;
    }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "out-flag");
    auto out = (tmp / "out.3mf").string();

    const auto in_size_before  = fs::file_size(in);
    const auto in_mtime_before = fs::last_write_time(in);

    auto r = run_cli({"plate", "add", in.string(),
                      "--name", "SideCar",
                      "--output", out});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    // Input untouched.
    REQUIRE(fs::file_size(in) == in_size_before);
    REQUIRE(fs::last_write_time(in) == in_mtime_before);
    REQUIRE(fs::exists(out));

    // Input must NOT have the new plate; output must.
    auto s_in  = load_project(in.string());
    auto s_out = load_project(out);
    auto has = [](const ProjectState& s, const std::string& n) {
        for (auto& p : s.plates) if (p->plate_name == n) return true;
        return false;
    };
    REQUIRE_FALSE(has(s_in,  "SideCar"));
    REQUIRE      (has(s_out, "SideCar"));
}
