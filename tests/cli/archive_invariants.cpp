#include "archive_invariants.hpp"

#include <catch2/catch_all.hpp>

#include <libslic3r/miniz_extension.hpp>

#include <algorithm>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace orca_cli_test::archive {

namespace {

struct ZipEntry {
    std::string                name;
    std::vector<unsigned char> bytes;
};

// In-memory unzip pass, scoped to this helper. We deliberately do not share
// src/cli/invariants.cpp's unzip routine here -- archive_invariants is a
// test-only header and the test target's expectations are slightly
// different (REQUIRE on archive-open failure rather than throw).
std::vector<ZipEntry> unzip_into_memory(const fs::path& zip)
{
    using namespace Slic3r;

    mz_zip_archive archive{};
    REQUIRE(open_zip_reader(&archive, zip.string()));

    std::vector<ZipEntry> out;
    const mz_uint n = mz_zip_reader_get_num_files(&archive);
    out.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&archive, i, &st)) continue;
        if (st.m_is_directory)                          continue;
        std::vector<unsigned char> bytes(static_cast<size_t>(st.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&archive, i, bytes.data(), bytes.size(), 0))
            continue;
        std::string name(st.m_filename);
        std::replace(name.begin(), name.end(), '\\', '/');
        out.push_back(ZipEntry{ std::move(name), std::move(bytes) });
    }
    close_zip_reader(&archive);
    return out;
}

const ZipEntry* find_entry(const std::vector<ZipEntry>& entries,
                           const std::string&           name)
{
    for (const auto& e : entries) if (e.name == name) return &e;
    return nullptr;
}

// PNG IHDR layout, after the 8-byte signature:
//   4 bytes: chunk length (big-endian)
//   4 bytes: chunk type "IHDR"
//   4 bytes: width  (big-endian)
//   4 bytes: height (big-endian)
//
// Returns {width, height} or {0, 0} on parse failure.
std::pair<uint32_t, uint32_t> parse_png_dimensions(
    const std::vector<unsigned char>& bytes)
{
    if (bytes.size() < 24) return {0, 0};
    // Signature: 0x89 P N G CR LF SUB LF
    static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) if (bytes[i] != kSig[i]) return {0, 0};
    if (bytes[12] != 'I' || bytes[13] != 'H' || bytes[14] != 'D' || bytes[15] != 'R')
        return {0, 0};
    auto be32 = [&](size_t off) {
        return (uint32_t(bytes[off]) << 24) |
               (uint32_t(bytes[off + 1]) << 16) |
               (uint32_t(bytes[off + 2]) << 8) |
                uint32_t(bytes[off + 3]);
    };
    return { be32(16), be32(20) };
}

std::string bytes_as_string(const std::vector<unsigned char>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

void assert_relationships_resolve(const fs::path& zip)
{
    auto entries = unzip_into_memory(zip);

    std::unordered_set<std::string> names;
    for (const auto& e : entries) names.insert(e.name);

    static const std::regex target_re(R"(Target=\"/?([^\"]+)\")");
    for (const auto& e : entries) {
        if (e.name.size() < 5 ||
            e.name.compare(e.name.size() - 5, 5, ".rels") != 0)
            continue;
        const std::string xml = bytes_as_string(e.bytes);
        for (auto it = std::sregex_iterator(xml.begin(), xml.end(), target_re);
             it != std::sregex_iterator{};
             ++it)
        {
            const std::string target = (*it)[1].str();
            INFO("dangling target=" << target << " in " << e.name);
            REQUIRE(names.count(target) == 1u);
        }
    }
}

void assert_plate_thumbnails_128(const fs::path& zip)
{
    auto entries = unzip_into_memory(zip);

    // PLATE_THUMBNAIL_SMALL_WIDTH / PLATE_THUMBNAIL_SMALL_HEIGHT in
    // src/libslic3r/Format/bbs_3mf.hpp are both 128 -- only the *_small.png
    // siblings are size-constrained to 128. The regular plate_N.png is the
    // full-resolution thumbnail (typically 512x512 from the GUI's render
    // path) and we deliberately do NOT pin it to a specific size here.
    static const std::regex small_png_re(
        R"(^Metadata/plate_\d+_small\.png$)");

    bool saw_any = false;
    for (const auto& e : entries) {
        if (!std::regex_match(e.name, small_png_re)) continue;
        saw_any = true;
        const auto [w, h] = parse_png_dimensions(e.bytes);
        INFO("entry=" << e.name << " parsed dims=" << w << "x" << h);
        REQUIRE(w == 128u);
        REQUIRE(h == 128u);
    }
    // It is fine for an archive to have zero plate PNGs (e.g. a test fixture
    // that strips thumbnails), but we want to know if assert_plate_thumbnails
    // ever runs on such an archive accidentally.
    if (!saw_any) {
        WARN("no plate_<N>_small.png entries found in " << zip.string());
    }
}

void assert_printable_area_4_points(const fs::path& zip)
{
    auto entries = unzip_into_memory(zip);
    const ZipEntry* cfg = find_entry(entries, "Metadata/project_settings.config");
    INFO("missing Metadata/project_settings.config in " << zip.string());
    REQUIRE(cfg != nullptr);

    const std::string json = bytes_as_string(cfg->bytes);

    // Find the printable_area key + the *next* array literal. The format is:
    //   "printable_area": [
    //       "0x0",
    //       "256x0",
    //       "256x256",
    //       "0x256"
    //   ],
    // Use a non-greedy multi-line regex to capture the array body. We do not
    // ship a JSON library in the test layer so this is the lightweight path.
    static const std::regex pa_re(
        R"("printable_area"\s*:\s*\[([^\]]*)\])");
    std::smatch m;
    INFO("printable_area key not found in project_settings.config");
    REQUIRE(std::regex_search(json, m, pa_re));

    // Count quoted string entries inside the array body.
    const std::string body = m[1].str();
    static const std::regex quoted_re(R"(\"[^\"]+\")");
    auto it  = std::sregex_iterator(body.begin(), body.end(), quoted_re);
    auto end = std::sregex_iterator{};
    const auto count = static_cast<size_t>(std::distance(it, end));
    INFO("printable_area body=" << body);
    REQUIRE(count == 4u);
}

void assert_parts_have_source_file(const fs::path& zip)
{
    auto entries = unzip_into_memory(zip);
    const ZipEntry* cfg = find_entry(entries, "Metadata/model_settings.config");
    INFO("missing Metadata/model_settings.config in " << zip.string());
    REQUIRE(cfg != nullptr);

    const std::string xml = bytes_as_string(cfg->bytes);

    // Match every <part ...> ... </part> block (or self-closing). For each,
    // assert that the block body contains a metadata entry with
    // key="source_file".
    static const std::regex part_block_re(
        R"(<part\b[^>]*>([\s\S]*?)</part>)");
    static const std::regex source_file_re(
        R"(<metadata\s+key=\"source_file\")");

    auto it  = std::sregex_iterator(xml.begin(), xml.end(), part_block_re);
    auto end = std::sregex_iterator{};
    size_t part_count = 0;
    for (; it != end; ++it) {
        ++part_count;
        const std::string body = (*it)[1].str();
        INFO("part #" << part_count << " body=" << body.substr(0, 200));
        REQUIRE(std::regex_search(body, source_file_re));
    }
    // If a project has no parts at all, that is fine (empty project). The
    // assertion is "every part that exists has source_file".
    if (part_count == 0)
        WARN("no <part> elements found in model_settings.config of "
             << zip.string());
}

void assert_object_extruder(const fs::path&    zip,
                            const std::string& object_name,
                            int                expected_extruder)
{
    auto entries = unzip_into_memory(zip);
    const ZipEntry* cfg = find_entry(entries, "Metadata/model_settings.config");
    INFO("missing Metadata/model_settings.config in " << zip.string());
    REQUIRE(cfg != nullptr);

    const std::string xml = bytes_as_string(cfg->bytes);

    // Walk every <object ...> ... </object> block and inspect its child
    // metadata entries. We do this with regex rather than a real XML parser
    // because the model_settings format is mechanically generated and stable
    // enough that the regex is unambiguous; pulling pugixml or expat in for
    // the test layer would be a heavier dependency than this warrants.
    static const std::regex object_block_re(
        R"(<object\b[^>]*>([\s\S]*?)</object>)");
    static const std::regex name_re(
        R"(<metadata\s+key=\"name\"\s+value=\"([^\"]+)\")");
    static const std::regex extruder_re(
        R"(<metadata\s+key=\"extruder\"\s+value=\"([^\"]+)\")");

    bool matched = false;
    int  actual_extruder = -1;
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), object_block_re);
         it != std::sregex_iterator{};
         ++it)
    {
        const std::string body = (*it)[1].str();
        std::smatch nm;
        if (!std::regex_search(body, nm, name_re)) continue;
        if (nm[1].str() != object_name)            continue;
        matched = true;
        std::smatch em;
        REQUIRE(std::regex_search(body, em, extruder_re));
        actual_extruder = std::stoi(em[1].str());
        break;
    }
    INFO("object_name=" << object_name);
    REQUIRE(matched);
    INFO("object=" << object_name
         << " expected_extruder=" << expected_extruder
         << " actual_extruder="   << actual_extruder);
    REQUIRE(actual_extruder == expected_extruder);
}

void assert_part_extruder(const fs::path&    zip,
                          const std::string& object_name,
                          const std::string& part_name,
                          int                expected_extruder)
{
    auto entries = unzip_into_memory(zip);
    const ZipEntry* cfg = find_entry(entries, "Metadata/model_settings.config");
    INFO("missing Metadata/model_settings.config in " << zip.string());
    REQUIRE(cfg != nullptr);

    const std::string xml = bytes_as_string(cfg->bytes);

    // Walk every <object ...> ... </object> block, find the one whose name
    // metadata matches object_name, then within it find the <part> whose name
    // metadata matches part_name, and assert its extruder metadata.
    static const std::regex object_block_re(
        R"(<object\b[^>]*>([\s\S]*?)</object>)");
    static const std::regex part_block_re(
        R"(<part\b[^>]*>([\s\S]*?)</part>)");
    static const std::regex name_re(
        R"(<metadata\s+key=\"name\"\s+value=\"([^\"]+)\")");
    static const std::regex extruder_re(
        R"(<metadata\s+key=\"extruder\"\s+value=\"([^\"]+)\")");

    bool matched_object = false;
    bool matched_part   = false;
    int  actual_extruder = -1;

    for (auto oit = std::sregex_iterator(xml.begin(), xml.end(), object_block_re);
         oit != std::sregex_iterator{};
         ++oit)
    {
        const std::string obj_body = (*oit)[1].str();
        std::smatch onm;
        if (!std::regex_search(obj_body, onm, name_re)) continue;
        if (onm[1].str() != object_name)                continue;
        matched_object = true;

        // Found the right object — now find the part by name inside it.
        for (auto pit = std::sregex_iterator(obj_body.begin(), obj_body.end(), part_block_re);
             pit != std::sregex_iterator{};
             ++pit)
        {
            const std::string part_body = (*pit)[1].str();
            std::smatch pnm;
            if (!std::regex_search(part_body, pnm, name_re)) continue;
            if (pnm[1].str() != part_name)                   continue;
            matched_part = true;

            std::smatch em;
            REQUIRE(std::regex_search(part_body, em, extruder_re));
            actual_extruder = std::stoi(em[1].str());
            break;
        }
        break;
    }

    INFO("object_name=" << object_name);
    REQUIRE(matched_object);
    INFO("part_name=" << part_name);
    REQUIRE(matched_part);
    INFO("part=" << part_name
         << " expected_extruder=" << expected_extruder
         << " actual_extruder="   << actual_extruder);
    REQUIRE(actual_extruder == expected_extruder);
}

void run_all_basic(const fs::path& zip)
{
    assert_relationships_resolve(zip);
    assert_plate_thumbnails_128(zip);
}

} // namespace orca_cli_test::archive
