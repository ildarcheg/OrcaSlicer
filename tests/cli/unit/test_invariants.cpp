// OrcaSlicer vendors Catch2 v3; umbrella header is catch_all.hpp.
#include <catch2/catch_all.hpp>
#include "invariants.hpp"
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
