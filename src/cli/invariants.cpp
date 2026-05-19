#include "invariants.hpp"
#include "io.hpp"

#include <libslic3r/miniz_extension.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <regex>
#include <unordered_set>

namespace orca_cli {

namespace {
// Type-driven vector-config check (per spec section 2, check 3). We iterate
// every option in print_config_def and consider it "vector-typed" if its
// declared ConfigOptionType is one of the coXxxs variants. This avoids the
// brittleness of a hardcoded key allow-list: any new vector key added to
// PrintConfig.cpp gets covered automatically.
bool is_vector_type(Slic3r::ConfigOptionType t) {
    using namespace Slic3r;
    switch (t) {
        case coFloats:
        case coInts:
        case coStrings:
        case coPercents:
        case coFloatsOrPercents:
        case coPoints:
        case coBools:
        case coEnums:
        // coPoint3 is a single 3d point; coPointsGroups / coIntsGroups are
        // already vector_type by virtue of being | coVectorType. We list
        // them explicitly so future readers don't have to chase the bit
        // arithmetic.
        case coPointsGroups:
        case coIntsGroups:
            return true;
        default:
            return false;
    }
}
} // namespace

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

void verify_vector_config_roundtrip(const ProjectState& in_memory,
                                    const std::string&  zip_path)
{
    using namespace Slic3r;

    // Re-parse the just-written archive into a fresh ProjectState so we can
    // compare every vector-typed project_config option against what was in
    // RAM before the save. A mismatch means save_bbs_3mf lost or mutated
    // information for that key on disk; that's the bug pattern we are
    // guarding against.
    const ProjectState loaded = load_project(zip_path);

    for (const auto& kv : print_config_def.options) {
        const std::string&     key = kv.first;
        const ConfigOptionDef& def = kv.second;
        if (!is_vector_type(def.type)) continue;

        const ConfigOption* a = in_memory.project_config->option(key);
        const ConfigOption* b = loaded   .project_config->option(key);

        // Both sides absent -> nothing to compare. Common case: project did
        // not set this option at all.
        if (a == nullptr && b == nullptr) continue;

        if ((a == nullptr) != (b == nullptr)) {
            throw InvariantViolation(
                "vector key " + key +
                " present in one side only after roundtrip");
        }

        // Cheap, stable comparison via the same serialization the on-disk
        // format uses. Avoids having to dispatch on every concrete vector
        // type.
        const std::string sa = a->serialize();
        const std::string sb = b->serialize();
        if (sa != sb) {
            throw InvariantViolation(
                "vector key " + key + " differs after roundtrip:\n"
                "  in-memory: " + sa + "\n"
                "  saved:     " + sb);
        }
    }
}

void run_all_invariants(const ProjectState& in_memory,
                        const std::string&  zip_path)
{
    auto entries = unzip_to_memory(zip_path);
    verify_relationships(entries);
    verify_plate_thumbnails(entries);
    verify_vector_config_roundtrip(in_memory, zip_path);
}

} // namespace orca_cli
