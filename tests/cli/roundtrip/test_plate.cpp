#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "io.hpp"
#include "project_ops.hpp"
#include "invariants.hpp"

#include <boost/filesystem.hpp>
#include <set>
#include <string>

using namespace orca_cli;
using namespace orca_cli_test;

TEST_CASE("orca-cli: add_plate round-trip preserves new plate with placeholder thumbnails",
          "[orca-cli][P2][roundtrip]")
{
    if (ref_3mf().empty()) {
        SUCCEED("Skipped: reference 3mf not available");
        return;
    }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "rt-add-plate");
    auto out = (tmp / "rt-add-plate-out.3mf").string();

    auto s = load_project(in.string());
    auto n_before = s.plates.size();
    add_plate(s, "RT_NewPlate");
    save_project(s, out);

    auto s2 = load_project(out);
    REQUIRE(s2.plates.size() == n_before + 1);
    bool found = false;
    for (auto& p : s2.plates)
        if (p->plate_name == "RT_NewPlate") found = true;
    REQUIRE(found);

    // Independent archive-level check: verify the produced .3mf passes the
    // runtime invariants. The placeholder injection in
    // passthrough_missing_thumbnails should have made
    // Metadata/plate_<n>.png and plate_<n>_small.png present for the new
    // plate, so verify_relationships and verify_plate_thumbnails pass.
    auto entries = unzip_to_memory(out);
    REQUIRE_NOTHROW(verify_relationships(entries));
    REQUIRE_NOTHROW(verify_plate_thumbnails(entries));
}

TEST_CASE("orca-cli: remove_plate drops orphaned PNGs from the output archive", "[orca-cli][P2][roundtrip]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "rt-remove-plate-pngs");
    auto out = (tmp / "rt-remove-plate-pngs-out.3mf").string();

    auto s = load_project(in.string());
    // Add a plate so the source has at least 3 plates (most test fixtures have 2).
    add_plate(s, "ToRemove");
    save_project(s, in.string());

    s = load_project(in.string());
    auto n_before = s.plates.size();
    // Remove the last plate.
    auto last_name = s.plates.back()->plate_name;
    remove_plate(s, last_name);
    save_project(s, out);

    // Re-open and check archive contents.
    auto entries = unzip_to_memory(out);
    std::set<std::string> names;
    for (auto& e : entries) names.insert(e.name);

    // No plate_<n_before>.png nor plate_<n_before>_small.png should remain.
    std::string orphan      = "Metadata/plate_" + std::to_string(n_before) + ".png";
    std::string orphan_sm   = "Metadata/plate_" + std::to_string(n_before) + "_small.png";
    REQUIRE_FALSE(names.count(orphan));
    REQUIRE_FALSE(names.count(orphan_sm));
}
