#include <catch2/catch_all.hpp>
#include "png_placeholder.hpp"

#include <miniz.h>

#include <cstdint>
#include <vector>

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

TEST_CASE("orca-cli: placeholder PNG every pixel byte equals 0xC0 after IDAT decompression", "[orca-cli][P2][unit]") {
    auto bytes = make_placeholder_png_128_gray_C0();

    // Locate the IDAT chunk. PNG layout:
    //   [0..7]   signature
    //   [8..11]  IHDR length (13)
    //   [12..15] "IHDR"
    //   [16..28] IHDR payload (13 bytes)
    //   [29..32] IHDR CRC
    //   [33..36] next chunk length
    //   [37..40] next chunk type
    //   ...
    // The IDAT chunk's payload is a zlib stream. After inflation it yields
    // 128 rows of (1 filter byte + 128 RGBA pixels) = 128 * (1 + 512) = 65664 bytes.

    size_t idat_off = 0;
    for (size_t i = 8; i < bytes.size() - 8; ) {
        uint32_t len = (uint32_t(uint8_t(bytes[i]))   << 24)
                     | (uint32_t(uint8_t(bytes[i+1])) << 16)
                     | (uint32_t(uint8_t(bytes[i+2])) << 8)
                     |  uint32_t(uint8_t(bytes[i+3]));
        bool is_idat = bytes[i+4] == 'I' && bytes[i+5] == 'D'
                    && bytes[i+6] == 'A' && bytes[i+7] == 'T';
        if (is_idat) { idat_off = i + 8; /* payload start */
                       INFO("IDAT chunk at offset " << i << " length " << len);
                       // Decompress using miniz.
                       std::vector<unsigned char> out(128 * (1 + 4 * 128) + 1024);
                       mz_ulong out_len = (mz_ulong)out.size();
                       int rc = mz_uncompress(out.data(), &out_len,
                                              reinterpret_cast<const unsigned char*>(bytes.data() + idat_off),
                                              len);
                       REQUIRE(rc == MZ_OK);
                       REQUIRE(out_len == 128u * (1u + 4u * 128u));
                       // Every scanline starts with a filter byte 0, then 512 bytes
                       // of RGBA pixels all == 0xC0.
                       for (size_t row = 0; row < 128; ++row) {
                           size_t base = row * (1 + 4 * 128);
                           REQUIRE(out[base] == 0);   // filter byte
                           for (size_t px = 0; px < 4 * 128; ++px) {
                               REQUIRE(static_cast<unsigned>(out[base + 1 + px]) == 0xC0u);
                           }
                       }
                       return;
        }
        i += 8 + len + 4; // length + type + payload + CRC
    }
    FAIL("IDAT chunk not found");
}
