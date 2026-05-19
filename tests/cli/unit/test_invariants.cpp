// OrcaSlicer vendors Catch2 v3; umbrella header is catch_all.hpp.
#include <catch2/catch_all.hpp>
#include "invariants.hpp"
#include "io.hpp"
#include "../test_common.hpp"
#include <cstring>
#include <vector>

using namespace orca_cli;

namespace {
// Helper -- wraps a literal C-string as the .bytes of a ZipEntry without the
// terminator. (std::vector<char> has no string-literal constructor.)
inline std::vector<char> bytes_of(const char* s) {
    return std::vector<char>(s, s + std::strlen(s));
}
} // namespace

TEST_CASE("orca-cli: verify_relationships passes on well-formed archive",
          "[orca-cli][P1][unit]") {
    const char* rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Relationships>"
        "  <Relationship Target=\"/3D/3dmodel.model\" Id=\"r1\""
        "                Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>"
        "</Relationships>";
    std::vector<ZipEntry> entries{
        { "_rels/.rels",      bytes_of(rels)        },
        { "3D/3dmodel.model", std::vector<char>{}   },
    };
    REQUIRE_NOTHROW(verify_relationships(entries));
}

TEST_CASE("orca-cli: verify_relationships throws on dangling Target",
          "[orca-cli][P1][unit]") {
    const char* rels =
        "<Relationships>"
        "  <Relationship Target=\"/Metadata/plate_9_small.png\" Id=\"r1\""
        "                Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail\"/>"
        "</Relationships>";
    std::vector<ZipEntry> entries{
        { "_rels/.rels", bytes_of(rels) },
    };
    REQUIRE_THROWS_AS(verify_relationships(entries), InvariantViolation);
}

TEST_CASE("orca-cli: verify_plate_thumbnails passes when every plate has png + small_png",
          "[orca-cli][P1][unit]") {
    std::vector<ZipEntry> entries{
        { "Metadata/plate_1.png",       std::vector<char>(8, '\x89') },
        { "Metadata/plate_1_small.png", std::vector<char>(8, '\x89') },
    };
    REQUIRE_NOTHROW(verify_plate_thumbnails(entries));
}

TEST_CASE("orca-cli: verify_plate_thumbnails throws when small_png is missing",
          "[orca-cli][P1][unit]") {
    std::vector<ZipEntry> entries{
        { "Metadata/plate_1.png", std::vector<char>(8, '\x89') },
    };
    REQUIRE_THROWS_AS(verify_plate_thumbnails(entries), InvariantViolation);
}

TEST_CASE("orca-cli: verify_vector_config_roundtrip passes on reference 3mf",
          "[orca-cli][P1][unit][needs-ref]") {
    using namespace orca_cli_test;
    if (ref_3mf().empty()) {
        SUCCEED("Skipped: reference 3mf not on host");
        return;
    }
    auto tmp   = make_temp_dir();
    auto out   = (tmp / "vectorcheck.3mf").string();
    auto state = orca_cli::load_project(ref_3mf().string());
    // save_project itself now calls run_all_invariants, including this very
    // check, so a green test here doubles as confirmation that the wired-in
    // guard does not false-positive.
    orca_cli::save_project(state, out);
    REQUIRE_NOTHROW(orca_cli::verify_vector_config_roundtrip(state, out));
}
