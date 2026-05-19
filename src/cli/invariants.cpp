#include "invariants.hpp"

#include <libslic3r/miniz_extension.hpp>

#include <regex>
#include <unordered_set>

namespace orca_cli {

std::vector<ZipEntry> unzip_to_memory(const std::string& zip_path)
{
    using namespace Slic3r;

    mz_zip_archive archive{};
    if (!open_zip_reader(&archive, zip_path))
        throw InvariantViolation("cannot open archive: " + zip_path);

    std::vector<ZipEntry> out;
    const mz_uint n = mz_zip_reader_get_num_files(&archive);
    out.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&archive, i, &st)) continue;
        if (st.m_is_directory)                          continue;

        std::vector<char> bytes(static_cast<size_t>(st.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&archive, i, bytes.data(), bytes.size(), 0))
            continue;
        out.push_back(ZipEntry{ st.m_filename, std::move(bytes) });
    }
    close_zip_reader(&archive);
    return out;
}

void verify_relationships(const std::vector<ZipEntry>& entries)
{
    // Names are case-sensitive inside a .3mf for the file table. Build a set
    // of every entry name for O(1) target-existence lookup.
    std::unordered_set<std::string> names;
    names.reserve(entries.size());
    for (const auto& e : entries)
        names.insert(e.name);

    // Match Target="..." in any *.rels file. The 3mf spec uses Target="/foo"
    // (leading slash optional), so capture the path with the slash stripped.
    static const std::regex target_re(R"(Target=\"/?([^\"]+)\")");

    for (const auto& e : entries) {
        if (e.name.size() < 5 ||
            e.name.compare(e.name.size() - 5, 5, ".rels") != 0)
            continue;

        const std::string xml(e.bytes.begin(), e.bytes.end());
        for (auto it = std::sregex_iterator(xml.begin(), xml.end(), target_re);
             it != std::sregex_iterator{};
             ++it)
        {
            const std::string& target = (*it)[1].str();
            if (names.find(target) == names.end()) {
                throw InvariantViolation(
                    "dangling relationship target: " + target +
                    " (declared in " + e.name + ")");
            }
        }
    }
}

void verify_plate_thumbnails(const std::vector<ZipEntry>& entries)
{
    std::unordered_set<std::string> names;
    names.reserve(entries.size());
    for (const auto& e : entries)
        names.insert(e.name);

    // Match "Metadata/plate_<N>.png" (the regular thumbnail) and require the
    // small variant alongside. We deliberately skip "_small.png" and
    // "_no_light.png" / "top_" / "pick_" siblings here -- the GUI's plate
    // panel only needs the small thumbnail next to the regular one.
    static const std::regex plate_re(R"(^Metadata/plate_(\d+)\.png$)");

    std::smatch m;
    for (const auto& e : entries) {
        if (!std::regex_match(e.name, m, plate_re)) continue;
        const std::string& n   = m[1].str();
        const std::string small = "Metadata/plate_" + n + "_small.png";
        if (names.find(small) == names.end()) {
            throw InvariantViolation(
                "plate " + n + " missing small thumbnail: " + small);
        }
    }
}

// verify_vector_config_roundtrip lands in Task 1.7.

} // namespace orca_cli
