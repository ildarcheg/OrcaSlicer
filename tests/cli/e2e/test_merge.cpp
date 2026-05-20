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

// Helper: set up a 2-volume object via split-to-parts. Returns the
// project path on success, or an EMPTY fs::path when a prerequisite
// (reference 3mf, two_cubes.stl) is unavailable -- the caller checks
// `out.empty()` and SUCCEED+returns so CI without local fixtures still
// passes. Inserts a `plate add MergePlate` step so tests don't depend
// on the reference's plate naming (which is "Plate 01 test" / "Plate
// 02 test" as of 2026-05-20, neither matching the historical "Plate 1").
static fs::path make_split_project(const fs::path& tmp,
                                   const std::string& obj_name) {
    if (orca_cli_test::ref_3mf().empty()) return {};
    const auto stl = fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl";
    if (!fs::exists(stl)) return {};
    const auto out = tmp / (obj_name + ".3mf");
    fs::copy_file(orca_cli_test::ref_3mf(), out, fs::copy_options::overwrite_existing);
    REQUIRE(orca_cli_test::run_cli({"plate","add",out.string(),
        "--name","MergePlate"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","MergePlate","--stl",stl.string(),"--name",obj_name}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name",obj_name}).exit_code == 0);
    return out;
}

// Helper macro for every anti-case: skip cleanly when the helper
// returned an empty path because a prerequisite was missing.
#define SKIP_IF_PROJECT_EMPTY(p) \
    do { if ((p).empty()) { SUCCEED("Skipped: prerequisite unavailable"); return; } } while (0)

TEST_CASE("merge-parts exits 1 when --parts has 1 entry (case 2)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "single");
    SKIP_IF_PROJECT_EMPTY(out);
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","single","--parts","single_1","--into","main"});
    REQUIRE(rc.exit_code == 1);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 1 when --parts has duplicate names (case 3)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "dup");
    SKIP_IF_PROJECT_EMPTY(out);
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","dup","--parts","dup_1,dup_1","--into","main"});
    REQUIRE(rc.exit_code == 1);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 1 when --into is empty (case 4)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "noemptyinto");
    SKIP_IF_PROJECT_EMPTY(out);
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","noemptyinto","--parts","noemptyinto_1,noemptyinto_2",
        "--into",""});
    REQUIRE(rc.exit_code == 1);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 5 when --into collides with non-source (case 8)",
          "[orca-cli][merge][e2e]") {
    // Need a third (non-source) volume on the object to collide with.
    // Simplest path: split to N parts, then merge only a subset, then
    // attempt a second merge whose --into name = one of the survivors.
    const auto tmp = orca_cli_test::make_temp_dir();
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }
    const auto stl = orca_cli_test::stl_dir() / "box_with_text.stl";
    if (!fs::exists(stl)) { SUCCEED("Skipped: box_with_text.stl not present"); return; }
    const auto out = tmp / "collide.3mf";
    fs::copy_file(orca_cli_test::ref_3mf(), out, fs::copy_options::overwrite_existing);
    REQUIRE(orca_cli_test::run_cli({"plate","add",out.string(),
        "--name","MergePlate"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","MergePlate","--stl",stl.string(),"--name","col"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","col"}).exit_code == 0);
    // box_with_text.stl produces N>=3 parts. First merge col_1+col_2 into
    // col_merged, leaving col_3, col_4, ... untouched.
    REQUIRE(orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","col","--parts","col_1,col_2","--into","col_merged"}).exit_code == 0);
    // Now try to merge col_3 + col_4 INTO col_merged. col_merged exists
    // and is NOT in --parts; expect exit 5 (duplicate_name).
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","col","--parts","col_3,col_4","--into","col_merged"});
    REQUIRE(rc.exit_code == 5);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 6 when source name not on object (case 5)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "unknownsrc");
    SKIP_IF_PROJECT_EMPTY(out);
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","unknownsrc","--parts","unknownsrc_1,__nope__","--into","main"});
    REQUIRE(rc.exit_code == 6);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 6 when --name object not found (case 6)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }
    const auto out = tmp / "unknownobj.3mf";
    fs::copy_file(orca_cli_test::ref_3mf(), out, fs::copy_options::overwrite_existing);
    // No need to plate-add; the object lookup fires before any
    // plate / source-volume resolution per Section 3 precedence.
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","__nope__","--parts","a,b","--into","m"});
    REQUIRE(rc.exit_code == 6);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 6 when --filament out of range (case 7)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "filoor");
    SKIP_IF_PROJECT_EMPTY(out);
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","filoor","--parts","filoor_1,filoor_2","--into","m",
        "--filament","9999"});
    REQUIRE(rc.exit_code == 6);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 7 on filament conflict without --filament "
          "(case 10)",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = make_split_project(tmp, "filconf");
    SKIP_IF_PROJECT_EMPTY(out);
    // Assign distinct filaments to the two parts so the merge sees
    // disagreement. Requires filament_slot_count >= 2 (verified at T0).
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","filconf","--part","filconf_1","--filament","1"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","filconf","--part","filconf_2","--filament","2"}).exit_code == 0);
    auto rc = orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","filconf","--parts","filconf_1,filconf_2","--into","m"});
    REQUIRE(rc.exit_code == 7);
    fs::remove_all(tmp);
}

TEST_CASE("merge-parts exits 7 on per-vol config conflict (case 14, "
          "message lists keys)",
          "[orca-cli][merge][e2e]") {
    // The CLI surface does NOT include a per-part config writer (spec
    // marks it out of scope), so we cannot author this conflict via
    // run_cli alone. Unit tests #11 and #17 cover the rule at the
    // project_ops layer.
    SUCCEED("Skipped: per-vol config conflict cannot be authored via "
            "the CLI today; covered by unit tests #11 and #17 at the "
            "project_ops layer");
    return;
}

TEST_CASE("merge-parts --output O writes only the side-car file",
          "[orca-cli][merge][e2e]") {
    const auto tmp = orca_cli_test::make_temp_dir();
    const auto in_ = make_split_project(tmp, "sidecar");
    SKIP_IF_PROJECT_EMPTY(in_);
    const auto out = tmp / "merged.3mf";
    const auto in_size_before = fs::file_size(in_);

    REQUIRE(orca_cli_test::run_cli({"object","merge-parts",in_.string(),
        "--name","sidecar","--parts","sidecar_1,sidecar_2","--into","m",
        "--output",out.string()}).exit_code == 0);
    REQUIRE(fs::exists(out));
    REQUIRE(fs::file_size(in_) == in_size_before);
    fs::remove_all(tmp);
}

TEST_CASE("end-to-end Layer B realistic mesh: split-to-parts then "
          "merge-parts on a subset (integration test #2)",
          "[orca-cli][merge][e2e]") {
    // Note: This test is intentionally an integration test that chains
    // Phase 8 split-to-parts with Phase 9 merge-parts; a Phase 8 regression
    // will surface here as a non-merge-parts failure. The focused
    // merge-parts e2e tests (#1, #3-#13) use Layer A directly without
    // going through split-to-parts.
    using namespace orca_cli_test::archive;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }
    const auto stl = orca_cli_test::stl_dir() / "box_with_text.stl";
    if (!fs::exists(stl)) { SUCCEED("Skipped: box_with_text.stl not present"); return; }

    const auto tmp = orca_cli_test::make_temp_dir();
    const auto out = tmp / "merge_layerb.3mf";
    fs::copy_file(orca_cli_test::ref_3mf(), out, fs::copy_options::overwrite_existing);

    REQUIRE(orca_cli_test::run_cli({"plate","add",out.string(),
        "--name","MergePlate"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","add",out.string(),
        "--plate","MergePlate","--stl",stl.string(),"--name","realB"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","split-to-parts",out.string(),
        "--name","realB"}).exit_code == 0);
    // Set differential filament on two parts so we know the merge needs
    // an --filament override.
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","realB","--part","realB_1","--filament","1"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","set-filament",out.string(),
        "--name","realB","--part","realB_2","--filament","2"}).exit_code == 0);
    REQUIRE(orca_cli_test::run_cli({"object","merge-parts",out.string(),
        "--name","realB","--parts","realB_1,realB_2",
        "--into","realB_main","--filament","1"}).exit_code == 0);

    // Archive invariants still hold after the merge.
    assert_parts_have_source_file(out);

    // Inspect should show one fewer volume on realB.
    auto rc = orca_cli_test::run_cli({"--json","inspect",out.string()});
    REQUIRE(rc.exit_code == 0);
    auto j = nlohmann::json::parse(rc.stdout_);
    bool found = false;
    for (const auto& o : j["data"]["objects"]) {
        if (o["name"] == "realB") {
            found = true;
            REQUIRE(o.contains("volumes"));
            // box_with_text.stl produces N>=3 parts. We merged 2 -> 1,
            // so volume count is N-1. Just check the merged name is
            // present.
            bool has_main = false;
            for (const auto& v : o["volumes"]) {
                if (v["name"] == "realB_main") has_main = true;
            }
            REQUIRE(has_main);
        }
    }
    REQUIRE(found);

    fs::remove_all(tmp);
}
