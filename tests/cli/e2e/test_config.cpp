#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "../archive_invariants.hpp"
#include "io.hpp"
#include "project_ops.hpp"

#include <nlohmann/json.hpp>

#include <libslic3r/Config.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <algorithm>
#include <boost/filesystem.hpp>
#include <string>
#include <vector>

using namespace orca_cli_test;

// --------------------------------------------------------------------------
// `config set`
// --------------------------------------------------------------------------

TEST_CASE("orca-cli: config set --key sparse_infill_density --value 30% succeeds",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-set-project");
    auto r = run_cli({"config", "set", in.string(),
                      "--key",   "sparse_infill_density",
                      "--value", "30%"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    const auto* opt = s.project_config->option("sparse_infill_density");
    REQUIRE(opt != nullptr);
    REQUIRE(opt->serialize() == "30%");
}

TEST_CASE("orca-cli: config set with --object writes per-object key",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-set-object");
    REQUIRE(run_cli({"plate",  "add", in.string(),
                     "--name", "C6"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "C6",
                     "--stl",   cube.string(),
                     "--name",  "cc"}).exit_code == 0);
    auto r = run_cli({"config", "set", in.string(),
                      "--object", "cc",
                      "--key",    "wall_loops",
                      "--value",  "4"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : s.model->objects)
        if (o->name == "cc") obj = o;
    REQUIRE(obj != nullptr);
    REQUIRE(obj->config.has("wall_loops"));
    REQUIRE(obj->config.opt_int("wall_loops") == 4);
}

TEST_CASE("orca-cli: config set unknown key returns bad_config",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-set-bad-key");
    auto r = run_cli({"config", "set", in.string(),
                      "--key",   "no_such_key",
                      "--value", "1"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 4); // bad_config
}

TEST_CASE("orca-cli: config set bad value returns bad_config",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-set-bad-val");
    auto r = run_cli({"config", "set", in.string(),
                      "--key",   "layer_height",
                      "--value", "not-a-num"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 4); // bad_config
}

TEST_CASE("orca-cli: config set with unknown object returns unknown_reference",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-set-ghost-obj");
    auto r = run_cli({"config", "set", in.string(),
                      "--object", "ghost",
                      "--key",    "wall_loops",
                      "--value",  "2"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 6); // unknown_reference
}

// --------------------------------------------------------------------------
// `config unset`
// --------------------------------------------------------------------------

TEST_CASE("orca-cli: config unset removes the key",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-unset");
    REQUIRE(run_cli({"config", "set", in.string(),
                     "--key",   "sparse_infill_density",
                     "--value", "30%"}).exit_code == 0);
    auto r = run_cli({"config", "unset", in.string(),
                      "--key", "sparse_infill_density"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    auto s = orca_cli::load_project(in.string());
    REQUIRE_FALSE(s.project_config->has("sparse_infill_density"));
}

TEST_CASE("orca-cli: config unset unknown key returns bad_config",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-unset-bad-key");
    auto r = run_cli({"config", "unset", in.string(),
                      "--key", "no_such_key"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 4); // bad_config
}

// --------------------------------------------------------------------------
// `config list`
// --------------------------------------------------------------------------

TEST_CASE("orca-cli: config list outputs project keys",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-list");
    auto r = run_cli({"config", "list", in.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    // The reference 3mf has a populated project_config, so the listing
    // must produce at least one stdout line ("<key> = <value>" form in
    // human mode, which is the default for run_cli).
    REQUIRE_FALSE(r.stdout_.empty());
}

TEST_CASE("orca-cli: config list --changed-only filters to differ-from-default",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-list-changed");
    auto r = run_cli({"config", "list", in.string(), "--changed-only"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    // The reference 3mf has non-default config (printer profile, filament
    // settings, ...). If the G6 default-comparison path crashed on a
    // coEnum, exit_code wouldn't be 0; if it short-circuited to empty,
    // stdout would be empty. Both states would fail this assertion.
    REQUIRE_FALSE(r.stdout_.empty());
}

TEST_CASE("orca-cli: config list rejects --output with usage_error",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-list-out");
    auto r = run_cli({"config", "list", in.string(),
                      "--output", (tmp / "x.3mf").string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 1); // usage_error
}

TEST_CASE("orca-cli: config list --object lists per-object keys after set",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-list-obj");
    REQUIRE(run_cli({"plate",  "add", in.string(), "--name", "L6"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "L6",
                     "--stl",   cube.string(),
                     "--name",  "lc"}).exit_code == 0);
    REQUIRE(run_cli({"config", "set", in.string(),
                     "--object", "lc",
                     "--key",    "wall_loops",
                     "--value",  "5"}).exit_code == 0);

    auto r = run_cli({"config", "list", in.string(), "--object", "lc"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_.find("wall_loops") != std::string::npos);
    REQUIRE(r.stdout_.find("5") != std::string::npos);
}

// --------------------------------------------------------------------------
// `--output` side-car round-trip on `config set`
// --------------------------------------------------------------------------

TEST_CASE("orca-cli: config set --output writes to side-car, leaves input untouched",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-set-output");
    auto out = (tmp / "out.3mf").string();
    const auto in_size_before = fs::file_size(in);
    auto r = run_cli({"config", "set", in.string(),
                      "--key",   "sparse_infill_density",
                      "--value", "42%",
                      "--output", out});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    // Input file untouched (size as a cheap proxy for byte-identity --
    // any mutation would have rewritten the 3mf and changed the file
    // size, even if only by a few bytes from a config-line delta).
    REQUIRE(fs::file_size(in) == in_size_before);
    REQUIRE(fs::exists(out));
    auto s_out = orca_cli::load_project(out);
    const auto* opt = s_out.project_config->option("sparse_infill_density");
    REQUIRE(opt != nullptr);
    REQUIRE(opt->serialize() == "42%");
}

TEST_CASE("orca-cli: config set adds key to different_settings_to_system in saved 3mf",
          "[orca-cli][P6][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cfg-set-diff-marker");
    REQUIRE(run_cli({"config", "set", in.string(),
                     "--key",   "sparse_infill_density",
                     "--value", "30%"}).exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    auto* diff = s.project_config->option<Slic3r::ConfigOptionStrings>("different_settings_to_system");
    REQUIRE(diff != nullptr);
    REQUIRE(diff->values.size() >= 2);
    std::vector<std::string> dirty;
    Slic3r::unescape_strings_cstyle(diff->values[0], dirty);
    REQUIRE(std::find(dirty.begin(), dirty.end(), "sparse_infill_density") != dirty.end());
}

// 2026-05-21 cross-project audit v2 follow-up: --count N produces N
// independent ModelObjects sharing one name. `config set --object NAME` must
// therefore apply to ALL of them (group-by-name), otherwise the user would
// have to issue one config set per clone.
TEST_CASE("orca-cli: config set --object stamps every clone in a --count cluster",
          "[orca-cli][P6][e2e][group-config-set]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "group-cfg-set");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "G"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "G",
                     "--stl",   cube.string(),
                     "--count", "3",
                     "--name",  "trio_c"}).exit_code == 0);
    REQUIRE(run_cli({"config", "set", in.string(),
                     "--object", "trio_c",
                     "--key", "wall_loops", "--value", "4"}).exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    int matches = 0;
    for (const auto* o : s.model->objects) {
        if (o->name != "trio_c") continue;
        ++matches;
        const auto* opt = o->config.get().opt<Slic3r::ConfigOptionInt>("wall_loops");
        REQUIRE(opt != nullptr);
        REQUIRE(opt->value == 4);
    }
    REQUIRE(matches == 3);
}

// `config unset --object NAME` symmetric to `set` for clone-groups.
TEST_CASE("orca-cli: config unset --object clears every clone in a --count cluster",
          "[orca-cli][P6][e2e][group-config-unset]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "group-cfg-unset");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "U"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "U",
                     "--stl",   cube.string(),
                     "--count", "3",
                     "--name",  "trio_u"}).exit_code == 0);
    // Set first via the (already group-aware) set, then unset.
    REQUIRE(run_cli({"config", "set", in.string(),
                     "--object", "trio_u",
                     "--key", "wall_loops", "--value", "4"}).exit_code == 0);
    REQUIRE(run_cli({"config", "unset", in.string(),
                     "--object", "trio_u",
                     "--key", "wall_loops"}).exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    int matches = 0;
    for (const auto* o : s.model->objects) {
        if (o->name != "trio_u") continue;
        ++matches;
        REQUIRE_FALSE(o->config.has("wall_loops"));
    }
    REQUIRE(matches == 3);
}

// `config list --object NAME` returns the union of keys across all matched
// objects, with values pulled from any matching object that has the key.
// Under normal usage (post-group-set), all clones agree.
TEST_CASE("orca-cli: config list --object lists the cluster's keyset",
          "[orca-cli][P6][e2e][group-config-list]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "group-cfg-list");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "L"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "L",
                     "--stl",   cube.string(),
                     "--count", "3",
                     "--name",  "trio_l"}).exit_code == 0);
    REQUIRE(run_cli({"config", "set", in.string(),
                     "--object", "trio_l",
                     "--key", "wall_loops", "--value", "5"}).exit_code == 0);

    auto r = run_cli({"--json", "config", "list", in.string(), "--object", "trio_l"});
    REQUIRE(r.exit_code == 0);
    auto j = nlohmann::json::parse(r.stdout_);
    REQUIRE(j["status"] == "ok");
    // The keys array must contain wall_loops with value "5".
    bool saw_wall_loops = false;
    for (const auto& kv : j["data"]["keys"]) {
        if (kv["key"] == "wall_loops") {
            REQUIRE(kv["value"] == "5");
            saw_wall_loops = true;
        }
    }
    REQUIRE(saw_wall_loops);
}
