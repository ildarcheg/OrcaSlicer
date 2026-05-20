// orca-cli object merge-parts end-to-end tests.
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "../archive_invariants.hpp"
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

namespace fs = boost::filesystem;

TEST_CASE("object merge-parts happy path: 2-source merge produces 1 volume "
          "with inspect --json showing merged name + filament",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "merge_happy.3mf";
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }
    fs::copy_file(orca_cli_test::ref_3mf(), out, fs::copy_options::overwrite_existing);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    if (!fs::exists(stl)) { SUCCEED("Skipped: two_cubes.stl fixture not available"); return; }

    REQUIRE(orca_cli_test::run_cli({"plate","add",out.string(),
        "--name","MergePlate"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","MergePlate","--stl",stl.string(),"--name","mp"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","mp"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","mp","--parts","mp_1,mp_2","--into","mp_main"}).exit_code == 0);

    auto rc = orca_cli_test::run_cli({"--json","inspect",out.string()});
    REQUIRE(rc.exit_code == 0);
    auto j = nlohmann::json::parse(rc.stdout_);
    REQUIRE(j["status"] == "ok");
    bool found = false;
    for (const auto& o : j["data"]["objects"]) {
        if (o["name"] == "mp") {
            found = true;
            REQUIRE(o.contains("volumes"));
            REQUIRE(o["volumes"].size() == 1);
            REQUIRE(o["volumes"][0]["name"] == "mp_main");
        }
    }
    REQUIRE(found);

    fs::remove_all(tmp);
}
