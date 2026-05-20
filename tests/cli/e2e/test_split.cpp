// orca-cli object split-to-parts end-to-end tests.
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "../archive_invariants.hpp"
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include <map>

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

TEST_CASE("inspect --json shows per-volume info for split objects",
          "[orca-cli][split][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    const auto tmp = make_temp_dir();
    const auto out = tmp / "split_inspect.3mf";
    fs::copy_file(ref_3mf(), out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    if (!fs::exists(stl)) { SUCCEED("Skipped: two_cubes.stl fixture not available"); return; }

    auto add_plate_rc = run_cli({"plate", "add", out.string(), "--name", "SplitPlate"});
    INFO("plate add stdout: " << add_plate_rc.stdout_ << "\nstderr: " << add_plate_rc.stderr_);
    REQUIRE(add_plate_rc.exit_code == 0);

    auto add_rc = run_cli({"object", "add", out.string(),
        "--plate", "SplitPlate", "--stl", stl.string(), "--name", "insp"});
    INFO("object add stdout: " << add_rc.stdout_ << "\nstderr: " << add_rc.stderr_);
    REQUIRE(add_rc.exit_code == 0);

    auto split_rc = run_cli({"object", "split-to-parts", out.string(), "--name", "insp"});
    INFO("split-to-parts stdout: " << split_rc.stdout_ << "\nstderr: " << split_rc.stderr_);
    REQUIRE(split_rc.exit_code == 0);

    auto sf1_rc = run_cli({"object", "set-filament", out.string(),
        "--name", "insp", "--part", "insp_1", "--filament", "1"});
    INFO("set-filament insp_1 stdout: " << sf1_rc.stdout_ << "\nstderr: " << sf1_rc.stderr_);
    REQUIRE(sf1_rc.exit_code == 0);

    auto sf2_rc = run_cli({"object", "set-filament", out.string(),
        "--name", "insp", "--part", "insp_2", "--filament", "2"});
    INFO("set-filament insp_2 stdout: " << sf2_rc.stdout_ << "\nstderr: " << sf2_rc.stderr_);
    REQUIRE(sf2_rc.exit_code == 0);

    auto rc = run_cli({"--json", "inspect", out.string()});
    INFO("inspect stdout: " << rc.stdout_ << "\nstderr: " << rc.stderr_);
    REQUIRE(rc.exit_code == 0);

    auto j = nlohmann::json::parse(rc.stdout_);
    REQUIRE(j["status"] == "ok");

    bool found = false;
    for (const auto& o : j["data"]["objects"]) {
        if (o["name"] == "insp") {
            found = true;
            REQUIRE(o.contains("volumes"));
            REQUIRE(o["volumes"].size() == 2);
            std::map<std::string, int> by_name;
            for (const auto& v : o["volumes"]) {
                by_name[v["name"].get<std::string>()] = v["extruder"].get<int>();
            }
            REQUIRE(by_name["insp_1"] == 1);
            REQUIRE(by_name["insp_2"] == 2);
        }
    }
    REQUIRE(found);

    fs::remove_all(tmp);
}

TEST_CASE("end-to-end split + per-part filament assignment passes archive invariants "
          "(Layer A)",
          "[orca-cli][split][e2e]") {
    using namespace orca_cli_test::archive;
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    const auto tmp = make_temp_dir();
    const auto out = tmp / "split_e2e_a.3mf";
    fs::copy_file(ref_3mf(), out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    if (!fs::exists(stl)) { SUCCEED("Skipped: two_cubes.stl fixture not available"); return; }

    auto add_plate_rc = run_cli({"plate", "add", out.string(), "--name", "SplitPlate"});
    INFO("plate add stdout: " << add_plate_rc.stdout_ << "\nstderr: " << add_plate_rc.stderr_);
    REQUIRE(add_plate_rc.exit_code == 0);

    auto add_rc = run_cli({"object", "add", out.string(),
        "--plate", "SplitPlate", "--stl", stl.string(), "--name", "e2e_a"});
    INFO("object add stdout: " << add_rc.stdout_ << "\nstderr: " << add_rc.stderr_);
    REQUIRE(add_rc.exit_code == 0);

    auto split_rc = run_cli({"object", "split-to-parts", out.string(),
        "--name", "e2e_a"});
    INFO("split-to-parts stdout: " << split_rc.stdout_ << "\nstderr: " << split_rc.stderr_);
    REQUIRE(split_rc.exit_code == 0);

    auto rc1 = run_cli({"object", "set-filament", out.string(),
        "--name", "e2e_a", "--part", "e2e_a_1", "--filament", "1"});
    INFO("set-filament part 1 stdout: " << rc1.stdout_ << "\nstderr: " << rc1.stderr_);
    REQUIRE(rc1.exit_code == 0);

    auto rc2 = run_cli({"object", "set-filament", out.string(),
        "--name", "e2e_a", "--part", "e2e_a_2", "--filament", "2"});
    INFO("set-filament part 2 stdout: " << rc2.stdout_ << "\nstderr: " << rc2.stderr_);
    REQUIRE(rc2.exit_code == 0);

    // Archive invariant #4 (Bug C): every <part> carries source_file.
    assert_parts_have_source_file(out);
    // Archive invariant #5: per-volume extruder is serialized.
    assert_part_extruder(out, "e2e_a", "e2e_a_1", 1);
    assert_part_extruder(out, "e2e_a", "e2e_a_2", 2);

    fs::remove_all(tmp);
}

TEST_CASE("object split-to-parts exits 7 on single-component object",
          "[orca-cli][split][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    const auto tmp = make_temp_dir();
    const auto out = tmp / "single.3mf";
    fs::copy_file(ref_3mf(), out);

    auto add_plate_rc = run_cli({"plate", "add", out.string(), "--name", "SinglePlate"});
    INFO("plate add stdout: " << add_plate_rc.stdout_ << "\nstderr: " << add_plate_rc.stderr_);
    REQUIRE(add_plate_rc.exit_code == 0);

    auto add_rc = run_cli({"object", "add", out.string(),
        "--plate", "SinglePlate",
        "--stl", (fs::path(ORCA_CLI_STL_DIR) / "000_01_test_cube.stl").string(),
        "--name", "cube"});
    INFO("object add stdout: " << add_rc.stdout_ << "\nstderr: " << add_rc.stderr_);
    REQUIRE(add_rc.exit_code == 0);

    auto rc = run_cli({"object", "split-to-parts", out.string(), "--name", "cube"});
    INFO("split-to-parts stdout: " << rc.stdout_ << "\nstderr: " << rc.stderr_);
    REQUIRE(rc.exit_code == 7);
    REQUIRE(rc.stderr_.find("only 1 connected") != std::string::npos);

    fs::remove_all(tmp);
}

TEST_CASE("object split-to-parts exits 7 on already-split object",
          "[orca-cli][split][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    const auto tmp = make_temp_dir();
    const auto out = tmp / "double.3mf";
    fs::copy_file(ref_3mf(), out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    if (!fs::exists(stl)) { SUCCEED("Skipped: two_cubes.stl fixture not available"); return; }

    auto add_plate_rc = run_cli({"plate", "add", out.string(), "--name", "SplitPlate"});
    INFO("plate add stdout: " << add_plate_rc.stdout_ << "\nstderr: " << add_plate_rc.stderr_);
    REQUIRE(add_plate_rc.exit_code == 0);

    auto add_rc = run_cli({"object", "add", out.string(),
        "--plate", "SplitPlate", "--stl", stl.string(), "--name", "x"});
    INFO("object add stdout: " << add_rc.stdout_ << "\nstderr: " << add_rc.stderr_);
    REQUIRE(add_rc.exit_code == 0);

    auto split_rc = run_cli({"object", "split-to-parts", out.string(), "--name", "x"});
    INFO("first split stdout: " << split_rc.stdout_ << "\nstderr: " << split_rc.stderr_);
    REQUIRE(split_rc.exit_code == 0);

    auto rc2 = run_cli({"object", "split-to-parts", out.string(), "--name", "x"});
    INFO("second split stdout: " << rc2.stdout_ << "\nstderr: " << rc2.stderr_);
    REQUIRE(rc2.exit_code == 7);
    REQUIRE(rc2.stderr_.find("multiple volumes") != std::string::npos);

    fs::remove_all(tmp);
}

TEST_CASE("object split-to-parts exits 6 on unknown object",
          "[orca-cli][split][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    const auto tmp = make_temp_dir();
    const auto out = tmp / "unknown.3mf";
    fs::copy_file(ref_3mf(), out);

    auto rc = run_cli({"object", "split-to-parts", out.string(), "--name", "__nope__"});
    INFO("split-to-parts stdout: " << rc.stdout_ << "\nstderr: " << rc.stderr_);
    REQUIRE(rc.exit_code == 6);

    fs::remove_all(tmp);
}

TEST_CASE("object set-filament --part exits 6 on unknown part name",
          "[orca-cli][split][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    const auto tmp = make_temp_dir();
    const auto out = tmp / "unknown_part.3mf";
    fs::copy_file(ref_3mf(), out);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    if (!fs::exists(stl)) { SUCCEED("Skipped: two_cubes.stl fixture not available"); return; }

    auto add_plate_rc = run_cli({"plate", "add", out.string(), "--name", "SplitPlate"});
    INFO("plate add stdout: " << add_plate_rc.stdout_ << "\nstderr: " << add_plate_rc.stderr_);
    REQUIRE(add_plate_rc.exit_code == 0);

    auto add_rc = run_cli({"object", "add", out.string(),
        "--plate", "SplitPlate", "--stl", stl.string(), "--name", "p"});
    INFO("object add stdout: " << add_rc.stdout_ << "\nstderr: " << add_rc.stderr_);
    REQUIRE(add_rc.exit_code == 0);

    auto split_rc = run_cli({"object", "split-to-parts", out.string(), "--name", "p"});
    INFO("split stdout: " << split_rc.stdout_ << "\nstderr: " << split_rc.stderr_);
    REQUIRE(split_rc.exit_code == 0);

    auto rc = run_cli({"object", "set-filament", out.string(),
        "--name", "p", "--part", "__nope__", "--filament", "2"});
    INFO("set-filament stdout: " << rc.stdout_ << "\nstderr: " << rc.stderr_);
    REQUIRE(rc.exit_code == 6);

    fs::remove_all(tmp);
}

TEST_CASE("object split-to-parts --output writes only the side-car file",
          "[orca-cli][split][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }

    const auto tmp = make_temp_dir();
    const auto in_ = tmp / "in.3mf";
    const auto out = tmp / "out.3mf";
    fs::copy_file(ref_3mf(), in_);
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    if (!fs::exists(stl)) { SUCCEED("Skipped: two_cubes.stl fixture not available"); return; }

    auto add_plate_rc = run_cli({"plate", "add", in_.string(), "--name", "SplitPlate"});
    INFO("plate add stdout: " << add_plate_rc.stdout_ << "\nstderr: " << add_plate_rc.stderr_);
    REQUIRE(add_plate_rc.exit_code == 0);

    auto add_rc = run_cli({"object", "add", in_.string(),
        "--plate", "SplitPlate", "--stl", stl.string(), "--name", "sc"});
    INFO("object add stdout: " << add_rc.stdout_ << "\nstderr: " << add_rc.stderr_);
    REQUIRE(add_rc.exit_code == 0);

    const auto in_size_before = fs::file_size(in_);

    auto split_rc = run_cli({"object", "split-to-parts", in_.string(),
        "--name", "sc", "--output", out.string()});
    INFO("split-to-parts stdout: " << split_rc.stdout_ << "\nstderr: " << split_rc.stderr_);
    REQUIRE(split_rc.exit_code == 0);

    REQUIRE(fs::exists(out));
    // Input file must be byte-identical after --output side-car write.
    REQUIRE(fs::file_size(in_) == in_size_before);

    fs::remove_all(tmp);
}
