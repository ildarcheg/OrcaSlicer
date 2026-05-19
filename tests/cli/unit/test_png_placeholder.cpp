#include <catch2/catch_all.hpp>
#include "png_placeholder.hpp"

#include <cstdint>

using namespace orca_cli;

TEST_CASE("orca-cli: placeholder PNG has correct signature and 128x128 IHDR",
          "[orca-cli][P2][unit]")
{
    auto bytes = make_placeholder_png_128_gray_C0();
    REQUIRE(bytes.size() > 100);
    REQUIRE(static_cast<unsigned char>(bytes[0]) == 0x89);
    REQUIRE(bytes[1] == 'P');
    REQUIRE(bytes[2] == 'N');
    REQUIRE(bytes[3] == 'G');

    auto be32 = [&](size_t o) {
        return (uint32_t(uint8_t(bytes[o]))     << 24)
             | (uint32_t(uint8_t(bytes[o + 1])) << 16)
             | (uint32_t(uint8_t(bytes[o + 2])) <<  8)
             |  uint32_t(uint8_t(bytes[o + 3]));
    };
    // After the 8-byte signature comes the IHDR chunk:
    //   offset  8..11: chunk length (always 13 for IHDR)
    //   offset 12..15: chunk type "IHDR"
    //   offset 16..19: width  (big-endian)
    //   offset 20..23: height (big-endian)
    REQUIRE(bytes[12] == 'I');
    REQUIRE(bytes[13] == 'H');
    REQUIRE(bytes[14] == 'D');
    REQUIRE(bytes[15] == 'R');
    REQUIRE(be32(16) == 128u);
    REQUIRE(be32(20) == 128u);
}

TEST_CASE("orca-cli: placeholder PNG ends with an IEND chunk",
          "[orca-cli][P2][unit]")
{
    auto bytes = make_placeholder_png_128_gray_C0();
    REQUIRE(bytes.size() >= 12);
    // Last 12 bytes are the IEND chunk: 4 length + 4 type + 4 crc (no payload).
    const size_t off = bytes.size() - 8;
    REQUIRE(bytes[off + 0] == 'I');
    REQUIRE(bytes[off + 1] == 'E');
    REQUIRE(bytes[off + 2] == 'N');
    REQUIRE(bytes[off + 3] == 'D');
}
