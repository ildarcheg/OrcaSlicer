#include "invariants.hpp"
#include "io.hpp"
#include "png_placeholder.hpp"

#include <libslic3r/miniz_extension.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <algorithm>
#include <optional>
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

std::vector<std::string> enumerate_zip_entry_names(const std::string& zip_path)
{
    std::vector<std::string> names;
    mz_zip_archive z{};
    if (!Slic3r::open_zip_reader(&z, zip_path)) return names;
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    names.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) continue;
        if (st.m_is_directory) continue;
        std::string name(st.m_filename);
        std::replace(name.begin(), name.end(), '\\', '/');
        names.push_back(std::move(name));
    }
    Slic3r::close_zip_reader(&z);
    return names;
}

std::optional<std::vector<unsigned char>>
extract_entry_to_memory(const std::string& zip_path, const std::string& entry_name)
{
    mz_zip_archive z{};
    if (!Slic3r::open_zip_reader(&z, zip_path)) return std::nullopt;
    int idx = mz_zip_reader_locate_file(&z, entry_name.c_str(), nullptr, 0);
    if (idx < 0) { Slic3r::close_zip_reader(&z); return std::nullopt; }
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&z, mz_uint(idx), &st)) {
        Slic3r::close_zip_reader(&z); return std::nullopt;
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(st.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(&z, mz_uint(idx), bytes.data(), bytes.size(), 0)) {
        Slic3r::close_zip_reader(&z); return std::nullopt;
    }
    Slic3r::close_zip_reader(&z);
    return bytes;
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
        const std::string& n = m[1].str();
        const auto t = orca_cli::plate_thumbnail_paths(std::stoi(n));
        if (names.find(t.small) == names.end()) {
            throw InvariantViolation(
                "plate " + n + " missing small thumbnail: " + t.small);
        }
    }
}

void verify_vector_config_roundtrip(const ProjectState& in_memory,
                                    const std::string&  zip_path)
{
    using namespace Slic3r;

    // Re-read only the config section of the just-written archive so we can
    // compare every vector-typed project_config option against what was in
    // RAM before the save. A mismatch means save_bbs_3mf lost or mutated
    // information for that key on disk; that's the bug pattern we are
    // guarding against.
    //
    // Use LoadStrategy::LoadConfig (skipping LoadModel) to avoid deserialising
    // mesh geometry, which is the dominant cost on every save path and
    // completely unnecessary for a config-key diff.
    DynamicPrintConfig roundtripped_config;
    {
        Model               dummy_model;
        PlateDataPtrs       plate_data;
        std::vector<Preset*> project_presets;
        bool   is_bbl_3mf  = false;
        bool   is_orca_3mf = false;
        Semver file_version;
        ConfigSubstitutionContext substitutions{
            ForwardCompatibilitySubstitutionRule::Disable};

        const bool ok = load_bbs_3mf(
            zip_path.c_str(),
            &roundtripped_config,
            &substitutions,
            &dummy_model,
            &plate_data,
            &project_presets,
            &is_bbl_3mf,
            &is_orca_3mf,
            &file_version,
            /*proFn*/ nullptr,
            LoadStrategy::LoadConfig);

        release_PlateData_list(plate_data);

        if (!ok)
            throw InvariantViolation(
                "invariant: failed to re-read project config from " + zip_path);
    }

    for (const auto& kv : print_config_def.options) {
        const std::string&     key = kv.first;
        const ConfigOptionDef& def = kv.second;
        if (!is_vector_type(def.type)) continue;

        const ConfigOption* a = in_memory.project_config->option(key);
        const ConfigOption* b = roundtripped_config.option(key);

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
    // Use lightweight helpers rather than a full-archive decompress.
    // verify_relationships only needs the _rels/.rels bytes; build a
    // minimal ZipEntry list with just that one entry so the existing
    // implementation can be reused without further signature changes.
    {
        auto rels_bytes = extract_entry_to_memory(zip_path, "_rels/.rels");
        if (!rels_bytes)
            throw InvariantViolation("cannot read _rels/.rels from: " + zip_path);
        std::vector<char> char_bytes(rels_bytes->begin(), rels_bytes->end());

        // We also need the entry-name set so verify_relationships can
        // confirm targets exist. Build a name-only ZipEntry list.
        auto entry_names = enumerate_zip_entry_names(zip_path);
        if (entry_names.empty())
            throw InvariantViolation("cannot open archive: " + zip_path);

        std::vector<ZipEntry> rel_entries;
        rel_entries.reserve(entry_names.size() + 1);
        // Put the real .rels bytes in first, then stub-out all other entries
        // (bytes empty — verify_relationships only reads bytes for .rels files).
        rel_entries.push_back(ZipEntry{ "_rels/.rels", std::move(char_bytes) });
        for (auto& n : entry_names) {
            if (n != "_rels/.rels")
                rel_entries.push_back(ZipEntry{ std::move(n), {} });
        }
        verify_relationships(rel_entries);
    }

    // verify_plate_thumbnails only needs the entry names.
    {
        auto entry_names = enumerate_zip_entry_names(zip_path);
        if (entry_names.empty())
            throw InvariantViolation("cannot open archive: " + zip_path);
        std::vector<ZipEntry> name_entries;
        name_entries.reserve(entry_names.size());
        for (auto& n : entry_names)
            name_entries.push_back(ZipEntry{ std::move(n), {} });
        verify_plate_thumbnails(name_entries);
    }

    verify_vector_config_roundtrip(in_memory, zip_path);
}

void verify_input_template_thumbnails(const std::string& zip_path,
                                      const std::string& display_path)
{
    // Open the staging copy via mz_zip_reader_init_file directly rather than
    // routing through Slic3r::open_zip_reader (boost::nowide::fopen). On
    // Windows, paths under %TEMP% can resolve to an 8.3 short form
    // (e.g. C:\Users\ILDARC~1\AppData\Local\Temp\...). boost::nowide's
    // UTF-8 -> UTF-16 round-trip can mishandle that form on some toolchains;
    // miniz's stdio path opens short-name paths reliably because the bytes
    // are pure ASCII. Sibling-parity with BambuStudio's
    // src/cli/invariant_guard.cpp::check_thumbnails_in_archive.
    //
    // TOCTOU defense (the staging .init-tmp pattern from
    // commands/project_init.cpp::do_project_init) is preserved by the
    // caller -- this function just operates on whatever path it's given.
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zip_path.c_str(), 0)) {
        // Open failure (locked / wrong path / genuinely corrupt). The right
        // remediation is "check the path", NOT "regenerate in the GUI" --
        // do not lead with the latter.
        throw InvariantViolation(
            "cannot open input template: " + display_path);
    }

    std::vector<ZipEntry> name_entries;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    name_entries.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        if (st.m_is_directory) continue;
        std::string name(st.m_filename);
        std::replace(name.begin(), name.end(), '\\', '/');
        name_entries.push_back(ZipEntry{ std::move(name), {} });
    }
    mz_zip_reader_end(&zip);

    try {
        verify_plate_thumbnails(name_entries);
    } catch (const InvariantViolation& e) {
        // Real thumbnail damage. The remediation IS "regenerate in the GUI".
        throw InvariantViolation(
            "input template has missing plate thumbnail(s); regenerate "
            "the template in OrcaSlicer GUI. (" + std::string(e.what()) + ")");
    }
}

} // namespace orca_cli
