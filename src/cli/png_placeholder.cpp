#include "png_placeholder.hpp"

#include <miniz.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace orca_cli {

PlateThumbnailPaths plate_thumbnail_paths(int n) {
    const std::string s = std::to_string(n);
    return PlateThumbnailPaths{
        "Metadata/plate_" + s + ".png",
        "Metadata/plate_" + s + "_small.png",
        "Metadata/plate_no_light_" + s + ".png",
        "Metadata/top_" + s + ".png",
        "Metadata/pick_" + s + ".png",
    };
}

namespace {

// Helpers for big-endian (network byte order) PNG fields.
void be32_append(std::vector<char>& out, uint32_t v)
{
    out.push_back(static_cast<char>((v >> 24) & 0xff));
    out.push_back(static_cast<char>((v >> 16) & 0xff));
    out.push_back(static_cast<char>((v >>  8) & 0xff));
    out.push_back(static_cast<char>( v        & 0xff));
}

// Append a PNG chunk: length(4) + type(4) + payload + crc32(type+payload).
void append_chunk(std::vector<char>&             out,
                  const char                     type[4],
                  const std::vector<uint8_t>&    payload)
{
    be32_append(out, static_cast<uint32_t>(payload.size()));
    const size_t crc_start = out.size();
    out.push_back(type[0]);
    out.push_back(type[1]);
    out.push_back(type[2]);
    out.push_back(type[3]);
    out.insert(out.end(),
               reinterpret_cast<const char*>(payload.data()),
               reinterpret_cast<const char*>(payload.data() + payload.size()));
    const auto crc = mz_crc32(0,
        reinterpret_cast<const unsigned char*>(out.data() + crc_start),
        out.size() - crc_start);
    be32_append(out, static_cast<uint32_t>(crc));
}

} // namespace

std::vector<char> make_placeholder_png_128_gray_C0()
{
    constexpr uint32_t W = 128;
    constexpr uint32_t H = 128;
    // Spec section 4.2: every pixel byte (R, G, B, AND alpha) equals 0xC0.
    // The function name encodes this contract -- "gray_C0" means all four
    // channels are 0xC0, not just RGB-with-opaque-alpha.
    constexpr uint8_t  GRAY = 0xC0;
    constexpr uint8_t  ALPHA = 0xC0;

    // Build the raw scanlines: each row starts with a filter byte (0 = None),
    // followed by W RGBA pixels.
    const size_t row_bytes  = 1 + size_t(W) * 4;          // 513
    const size_t raw_size   = row_bytes * size_t(H);      // 65664
    std::vector<uint8_t> raw(raw_size);
    for (uint32_t y = 0; y < H; ++y) {
        uint8_t* row = raw.data() + size_t(y) * row_bytes;
        row[0] = 0; // filter: None
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t* px = row + 1 + size_t(x) * 4;
            px[0] = GRAY;
            px[1] = GRAY;
            px[2] = GRAY;
            px[3] = ALPHA;
        }
    }

    // zlib-compress (PNG IDAT is a zlib stream, which is what mz_compress2
    // emits -- it wraps a raw deflate stream in zlib's 2-byte header + 4-byte
    // adler32 trailer). Level 1 is enough; the data is highly uniform.
    mz_ulong comp_cap = mz_compressBound(static_cast<mz_ulong>(raw_size));
    std::vector<uint8_t> compressed(static_cast<size_t>(comp_cap));
    mz_ulong comp_len = comp_cap;
    int rc = mz_compress2(compressed.data(), &comp_len,
                          raw.data(), static_cast<mz_ulong>(raw_size),
                          /*level=*/1);
    if (rc != MZ_OK)
        throw std::runtime_error("mz_compress2 failed in PNG placeholder");
    compressed.resize(static_cast<size_t>(comp_len));

    std::vector<char> out;
    out.reserve(8 + 12 + 13 + 12 + compressed.size() + 12);

    // 8-byte PNG signature.
    static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<char>(kSig[i]));

    // IHDR (13 bytes): width + height + bit_depth + color_type + compression
    // + filter + interlace.
    std::vector<uint8_t> ihdr;
    ihdr.reserve(13);
    auto be32_to_vec = [&](uint32_t v) {
        ihdr.push_back(uint8_t((v >> 24) & 0xff));
        ihdr.push_back(uint8_t((v >> 16) & 0xff));
        ihdr.push_back(uint8_t((v >>  8) & 0xff));
        ihdr.push_back(uint8_t( v        & 0xff));
    };
    be32_to_vec(W);
    be32_to_vec(H);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // color type: 6 = RGBA (truecolor with alpha)
    ihdr.push_back(0);  // compression method (only 0 is defined)
    ihdr.push_back(0);  // filter method (only 0 is defined)
    ihdr.push_back(0);  // interlace method (0 = no interlace)
    append_chunk(out, "IHDR", ihdr);

    // IDAT: the zlib-compressed scanline stream.
    std::vector<uint8_t> idat(compressed.begin(), compressed.end());
    append_chunk(out, "IDAT", idat);

    // IEND: empty payload.
    append_chunk(out, "IEND", {});

    return out;
}

} // namespace orca_cli
