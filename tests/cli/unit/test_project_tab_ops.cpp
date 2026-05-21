// tests/cli/unit/test_project_tab_ops.cpp
#include <catch2/catch_all.hpp>
#include "project_tab_ops.hpp"
#include "project_ops.hpp"
#include "io.hpp"
#include "../test_common.hpp"

#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <boost/filesystem.hpp>

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>

using namespace orca_cli;

// Helper: construct an empty ProjectState with a single empty plate and
// nothing in model.model_info / model.profile_info. Mirrors the pattern in
// tests/cli/unit/test_project_ops.cpp for plate tests.
static ProjectState make_empty_state() {
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    return s;
}

TEST_CASE("orca-cli: info_view on empty model returns six empty strings",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    auto v = info_view(s);
    REQUIRE(v.title.empty());
    REQUIRE(v.description.empty());
    REQUIRE(v.license.empty());
    REQUIRE(v.copyright.empty());
    REQUIRE(v.cover.empty());
    REQUIRE(v.origin.empty());
}

TEST_CASE("orca-cli: info_view returns populated ModelInfo fields verbatim",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    s.model->model_info = std::make_shared<Slic3r::ModelInfo>();
    s.model->model_info->model_name  = "T";
    s.model->model_info->description = "D";
    s.model->model_info->license     = "MIT";
    s.model->model_info->copyright   = R"([{"author":"Z"}])";
    s.model->model_info->cover_file  = "Auxiliaries/.thumbnails/thumbnail_3mf.png";
    s.model->model_info->origin      = "OrcaSlicer";
    auto v = info_view(s);
    REQUIRE(v.title       == "T");
    REQUIRE(v.description == "D");
    REQUIRE(v.license     == "MIT");
    REQUIRE(v.copyright   == R"([{"author":"Z"}])");
    REQUIRE(v.cover       == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    REQUIRE(v.origin      == "OrcaSlicer");
}

TEST_CASE("orca-cli: any_field_set(InfoSetParams) detects every field individually",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE(!any_field_set(InfoSetParams{}));
    InfoSetParams p;
    p.title = "x";       REQUIRE(any_field_set(p));
    p = {}; p.description = "x"; REQUIRE(any_field_set(p));
    p = {}; p.license     = "x"; REQUIRE(any_field_set(p));
    p = {}; p.copyright   = "x"; REQUIRE(any_field_set(p));
    p = {}; p.cover       = boost::filesystem::path("x.png"); REQUIRE(any_field_set(p));
}

TEST_CASE("orca-cli: info_set allocates model_info when nullptr",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE(s.model->model_info == nullptr);
    InfoSetParams p; p.title = "Smoke";
    info_set(s, p);
    REQUIRE(s.model->model_info != nullptr);
    REQUIRE(s.model->model_info->model_name == "Smoke");
}

TEST_CASE("orca-cli: info_set batches multiple fields in one call",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    InfoSetParams p;
    p.title       = "T";
    p.description = "D";
    p.license     = "MIT";
    p.copyright   = "(c) 2026";
    info_set(s, p);
    REQUIRE(s.model->model_info->model_name  == "T");
    REQUIRE(s.model->model_info->description == "D");
    REQUIRE(s.model->model_info->license     == "MIT");
    REQUIRE(s.model->model_info->copyright   == "(c) 2026");
}

TEST_CASE("orca-cli: info_set is idempotent (re-set same value is fine)",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    InfoSetParams p; p.title = "T";
    info_set(s, p);
    info_set(s, p);
    REQUIRE(s.model->model_info->model_name == "T");
}

TEST_CASE("orca-cli: allowed_info_fields lists exactly the five legal names",
          "[orca-cli][project-tab][unit]")
{
    const auto& names = allowed_info_fields();
    REQUIRE(names.size() == 5);
    REQUIRE(std::count(names.begin(), names.end(), "title")       == 1);
    REQUIRE(std::count(names.begin(), names.end(), "description") == 1);
    REQUIRE(std::count(names.begin(), names.end(), "license")     == 1);
    REQUIRE(std::count(names.begin(), names.end(), "copyright")   == 1);
    REQUIRE(std::count(names.begin(), names.end(), "cover")       == 1);
}

TEST_CASE("orca-cli: info_clear nulls a single named string field",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    InfoSetParams p; p.title = "T"; p.description = "D";
    info_set(s, p);
    info_clear(s, {"title"});
    REQUIRE(s.model->model_info->model_name.empty());
    REQUIRE(s.model->model_info->description == "D");  // untouched
}

TEST_CASE("orca-cli: info_clear nulls multiple fields in one call",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    InfoSetParams p;
    p.title = "T"; p.description = "D"; p.license = "MIT"; p.copyright = "C";
    info_set(s, p);
    info_clear(s, {"title", "license"});
    REQUIRE(s.model->model_info->model_name.empty());
    REQUIRE(s.model->model_info->license.empty());
    REQUIRE(s.model->model_info->description == "D");
    REQUIRE(s.model->model_info->copyright   == "C");
}

TEST_CASE("orca-cli: info_clear rejects unknown field with InvalidField",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE_THROWS_AS(info_clear(s, {"profile_title"}), InvalidField);
    REQUIRE_THROWS_AS(info_clear(s, {"title", "bogus"}), InvalidField);
}

TEST_CASE("orca-cli: info_clear is idempotent on an already-empty field",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE_NOTHROW(info_clear(s, {"title"}));  // model_info still nullptr
    InfoSetParams p; p.title = "X"; info_set(s, p);
    REQUIRE_NOTHROW(info_clear(s, {"title"}));
    REQUIRE_NOTHROW(info_clear(s, {"title"}));  // double-clear no-op
    REQUIRE(s.model->model_info->model_name.empty());
}

TEST_CASE("orca-cli: is_png accepts valid 8-byte PNG signature",
          "[orca-cli][project-tab][unit]")
{
    auto p = orca_cli_test::make_temp_dir() / "valid.png";
    {
        std::ofstream f(p.string(), std::ios::binary);
        const unsigned char sig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
        f.write(reinterpret_cast<const char*>(sig), 8);
    }
    REQUIRE(is_png(p));
}

TEST_CASE("orca-cli: is_png rejects JPG signature",
          "[orca-cli][project-tab][unit]")
{
    auto p = orca_cli_test::make_temp_dir() / "actually.jpg";
    {
        std::ofstream f(p.string(), std::ios::binary);
        const unsigned char sig[8] = {0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46};
        f.write(reinterpret_cast<const char*>(sig), 8);
    }
    REQUIRE_FALSE(is_png(p));
}

TEST_CASE("orca-cli: is_png rejects truncated files (<8 bytes)",
          "[orca-cli][project-tab][unit]")
{
    auto p = orca_cli_test::make_temp_dir() / "tiny.png";
    {
        std::ofstream f(p.string(), std::ios::binary);
        const unsigned char sig[4] = {0x89,0x50,0x4E,0x47};  // only 4 of 8
        f.write(reinterpret_cast<const char*>(sig), 4);
    }
    REQUIRE_FALSE(is_png(p));
}

TEST_CASE("orca-cli: is_png returns false on missing file (no throw)",
          "[orca-cli][project-tab][unit]")
{
    auto p = orca_cli_test::make_temp_dir() / "does_not_exist.png";
    REQUIRE_FALSE(is_png(p));
}

namespace {
// Test helper: write a valid 1x1 transparent PNG to `p`. Returns the bytes
// written so tests can byte-compare after roundtrip.
inline std::vector<unsigned char> write_tiny_png(const boost::filesystem::path& p) {
    static const unsigned char kPng[] = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,
        0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
        0x08,0x06,0x00,0x00,0x00,0x1F,0x15,0xC4,
        0x89,0x00,0x00,0x00,0x0D,0x49,0x44,0x41,
        0x54,0x78,0x9C,0x63,0x00,0x01,0x00,0x00,
        0x05,0x00,0x01,0x0D,0x0A,0x2D,0xB4,0x00,
        0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,
        0x42,0x60,0x82,
    };
    std::ofstream f(p.string(), std::ios::binary);
    f.write(reinterpret_cast<const char*>(kPng), sizeof(kPng));
    return std::vector<unsigned char>(kPng, kPng + sizeof(kPng));
}

inline std::vector<unsigned char> read_all(const boost::filesystem::path& p) {
    std::ifstream f(p.string(), std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),
                                      std::istreambuf_iterator<char>{});
}
} // namespace

TEST_CASE("orca-cli: embed_cover_image accepts PNG and points info cover_file at canonical path",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "hero.png";
    auto bytes = write_tiny_png(src);

    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Info);

    REQUIRE(s.model->model_info != nullptr);
    REQUIRE(s.model->model_info->cover_file
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");

    auto aux = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto landed = aux / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(boost::filesystem::exists(landed));
    REQUIRE(read_all(landed) == bytes);
}

TEST_CASE("orca-cli: embed_cover_image profile target sets ProfileCover and reuses canonical path",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "hero.png";
    write_tiny_png(src);

    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Profile);

    REQUIRE(s.model->profile_info != nullptr);
    REQUIRE(s.model->profile_info->ProfileCover
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
}

TEST_CASE("orca-cli: embed_cover_image second call overwrites canonical file bytes",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto a = tmp / "a.png"; auto bytes_a = write_tiny_png(a);
    auto b = tmp / "b.png";
    // Make b a distinct PNG: copy a's bytes, then bump one byte in the IDAT
    // payload zone (bytes 41-53 are the deflated image data; index 50 is
    // a safe tweak that keeps the file size constant -- we don't decode it).
    {
        auto bytes_b_src = bytes_a;
        bytes_b_src[50] ^= 0x7F;
        std::ofstream f(b.string(), std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes_b_src.data()),
                bytes_b_src.size());
    }
    auto bytes_b = read_all(b);
    REQUIRE(bytes_a != bytes_b);   // pre-condition guard

    auto s = make_empty_state();
    embed_cover_image(s, a, CoverTarget::Info);
    embed_cover_image(s, b, CoverTarget::Profile);  // overwrites a's bytes

    auto aux = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto landed = aux / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(read_all(landed) == bytes_b);
    REQUIRE(s.model->model_info->cover_file
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    REQUIRE(s.model->profile_info->ProfileCover
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
}

TEST_CASE("orca-cli: embed_cover_image rejects JPG with BadCoverImage",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto jpg = tmp / "fake.png";  // .png extension, JPG bytes
    {
        std::ofstream f(jpg.string(), std::ios::binary);
        const unsigned char sig[16] = {
            0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46,
            0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,
        };
        f.write(reinterpret_cast<const char*>(sig), 16);
    }
    auto s = make_empty_state();
    REQUIRE_THROWS_AS(embed_cover_image(s, jpg, CoverTarget::Info), BadCoverImage);
    REQUIRE(s.model->model_info == nullptr);  // not allocated on failure
}

TEST_CASE("orca-cli: embed_cover_image rejects missing source with BadCoverImage",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto missing = tmp / "nope.png";
    auto s = make_empty_state();
    REQUIRE_THROWS_AS(embed_cover_image(s, missing, CoverTarget::Info), BadCoverImage);
}

TEST_CASE("orca-cli: info_set --cover routes through embed_cover_image",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "via.png"; write_tiny_png(src);
    auto s = make_empty_state();
    InfoSetParams p; p.cover = src;
    info_set(s, p);
    REQUIRE(s.model->model_info->cover_file
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
}

TEST_CASE("orca-cli: clear_cover_image — only-info clear deletes the file",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "c.png"; write_tiny_png(src);
    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Info);
    auto landed = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
                  / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(boost::filesystem::exists(landed));

    clear_cover_image(s, CoverTarget::Info);
    REQUIRE(s.model->model_info->cover_file.empty());
    REQUIRE_FALSE(boost::filesystem::exists(landed));
}

TEST_CASE("orca-cli: clear_cover_image — only-profile clear deletes the file",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "c.png"; write_tiny_png(src);
    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Profile);
    auto landed = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
                  / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(boost::filesystem::exists(landed));

    clear_cover_image(s, CoverTarget::Profile);
    REQUIRE(s.model->profile_info->ProfileCover.empty());
    REQUIRE_FALSE(boost::filesystem::exists(landed));
}

TEST_CASE("orca-cli: clear_cover_image — both-set clear-info keeps file (profile still owns)",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "c.png"; write_tiny_png(src);
    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Info);
    embed_cover_image(s, src, CoverTarget::Profile);
    auto landed = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
                  / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(boost::filesystem::exists(landed));

    clear_cover_image(s, CoverTarget::Info);
    REQUIRE(s.model->model_info->cover_file.empty());
    REQUIRE(s.model->profile_info->ProfileCover
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    REQUIRE(boost::filesystem::exists(landed));  // kept!
}

TEST_CASE("orca-cli: clear_cover_image — both-set sequential clears delete on second",
          "[orca-cli][project-tab][unit]")
{
    auto tmp = orca_cli_test::make_temp_dir();
    auto src = tmp / "c.png"; write_tiny_png(src);
    auto s = make_empty_state();
    embed_cover_image(s, src, CoverTarget::Info);
    embed_cover_image(s, src, CoverTarget::Profile);
    auto landed = boost::filesystem::path(s.model->get_auxiliary_file_temp_path())
                  / ".thumbnails" / "thumbnail_3mf.png";

    clear_cover_image(s, CoverTarget::Info);
    REQUIRE(boost::filesystem::exists(landed));  // profile still owns

    clear_cover_image(s, CoverTarget::Profile);
    REQUIRE(s.model->profile_info->ProfileCover.empty());
    REQUIRE_FALSE(boost::filesystem::exists(landed));
}

TEST_CASE("orca-cli: clear_cover_image is idempotent (no-op on already-empty)",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE_NOTHROW(clear_cover_image(s, CoverTarget::Info));
    REQUIRE_NOTHROW(clear_cover_image(s, CoverTarget::Profile));
    // info_clear --field cover when model_info is nullptr also OK:
    REQUIRE_NOTHROW(info_clear(s, {"cover"}));
}

TEST_CASE("orca-cli: profile_view on empty model returns five empty strings",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    auto v = profile_view(s);
    REQUIRE(v.title.empty());
    REQUIRE(v.description.empty());
    REQUIRE(v.cover.empty());
    REQUIRE(v.user_id.empty());
    REQUIRE(v.user_name.empty());
}

TEST_CASE("orca-cli: profile_view surfaces user_id and user_name (read-only)",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    s.model->profile_info = std::make_shared<Slic3r::ModelProfileInfo>();
    s.model->profile_info->ProfileTile        = "T";
    s.model->profile_info->ProfileDescription = "D";
    s.model->profile_info->ProfileCover       = "Auxiliaries/.thumbnails/thumbnail_3mf.png";
    s.model->profile_info->ProfileUserId      = "id-42";
    s.model->profile_info->ProfileUserName    = "alice";
    auto v = profile_view(s);
    REQUIRE(v.title       == "T");
    REQUIRE(v.description == "D");
    REQUIRE(v.cover       == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    REQUIRE(v.user_id     == "id-42");
    REQUIRE(v.user_name   == "alice");
}

TEST_CASE("orca-cli: profile_set allocates profile_info; batches fields",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE(s.model->profile_info == nullptr);
    ProfileSetParams p; p.title = "PT"; p.description = "PD";
    profile_set(s, p);
    REQUIRE(s.model->profile_info != nullptr);
    REQUIRE(s.model->profile_info->ProfileTile        == "PT");
    REQUIRE(s.model->profile_info->ProfileDescription == "PD");
}

TEST_CASE("orca-cli: allowed_profile_fields lists exactly three legal names",
          "[orca-cli][project-tab][unit]")
{
    const auto& n = allowed_profile_fields();
    REQUIRE(n.size() == 3);
    REQUIRE(std::count(n.begin(), n.end(), "title")       == 1);
    REQUIRE(std::count(n.begin(), n.end(), "description") == 1);
    REQUIRE(std::count(n.begin(), n.end(), "cover")       == 1);
}

TEST_CASE("orca-cli: profile_clear rejects info-only fields with InvalidField",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    REQUIRE_THROWS_AS(profile_clear(s, {"license"}),   InvalidField);
    REQUIRE_THROWS_AS(profile_clear(s, {"user_id"}),   InvalidField);
}

TEST_CASE("orca-cli: sanitize_aux_name accepts legitimate filenames",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE(sanitize_aux_name("model.stl")              == "model.stl");
    REQUIRE(sanitize_aux_name("assembly_step_1.png")    == "assembly_step_1.png");
    REQUIRE(sanitize_aux_name("Bill of Materials.pdf")  == "Bill of Materials.pdf");
    REQUIRE(sanitize_aux_name("a.b.c.png")              == "a.b.c.png");
    REQUIRE(sanitize_aux_name("CON_NotReserved.txt")    == "CON_NotReserved.txt");
}

TEST_CASE("orca-cli: sanitize_aux_name rejects path-traversal and separators",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE_THROWS_AS(sanitize_aux_name(""),              AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("a/b"),           AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("a\\b"),          AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name(std::string("a\0b", 3)), AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("."),             AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name(".."),            AuxNameError);
}

TEST_CASE("orca-cli: sanitize_aux_name rejects leading/trailing dot or whitespace",
          "[orca-cli][project-tab][unit]")
{
    REQUIRE_THROWS_AS(sanitize_aux_name(".hidden.png"),   AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("trail."),        AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name(" leading.png"),  AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("trailing.png "), AuxNameError);
    REQUIRE_THROWS_AS(sanitize_aux_name("\ttab.png"),     AuxNameError);
}

TEST_CASE("orca-cli: sanitize_aux_name rejects Windows reserved names (case-insensitive, with extension)",
          "[orca-cli][project-tab][unit]")
{
    for (const std::string& n : {"CON", "con", "Con", "PRN", "AUX", "NUL",
                                 "COM1", "com9", "LPT1", "lpt9"}) {
        DYNAMIC_SECTION("bare: " << n) {
            REQUIRE_THROWS_AS(sanitize_aux_name(n), AuxNameError);
        }
        DYNAMIC_SECTION("with ext: " << n + ".png") {
            REQUIRE_THROWS_AS(sanitize_aux_name(n + ".png"), AuxNameError);
        }
    }
    // Boundary: COM10 / LPT10 are NOT reserved.
    REQUIRE(sanitize_aux_name("COM10.png") == "COM10.png");
    REQUIRE(sanitize_aux_name("LPT10.png") == "LPT10.png");
}

TEST_CASE("orca-cli: aux_list returns empty four-bucket result on fresh project",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    auto entries = aux_list(s);
    REQUIRE(entries.empty());
}

TEST_CASE("orca-cli: aux_list walks every populated bucket and stamps size",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    auto aux_root = boost::filesystem::path(s.model->get_auxiliary_file_temp_path());
    auto pics = aux_root / folder_subdir(AuxFolder::pictures);
    auto bom  = aux_root / folder_subdir(AuxFolder::bom);
    boost::filesystem::create_directories(pics);
    boost::filesystem::create_directories(bom);
    {
        std::ofstream(pics.string() + "/hero.png", std::ios::binary).write("HELLO", 5);
        std::ofstream(bom.string()  + "/parts.csv", std::ios::binary).write("a,b,c\n", 6);
    }

    auto entries = aux_list(s);
    REQUIRE(entries.size() == 2u);
    bool saw_hero = false, saw_parts = false;
    for (const auto& e : entries) {
        if (e.folder == AuxFolder::pictures && e.name == "hero.png") {
            REQUIRE(e.size == 5u); saw_hero = true;
        }
        if (e.folder == AuxFolder::bom && e.name == "parts.csv") {
            REQUIRE(e.size == 6u); saw_parts = true;
        }
    }
    REQUIRE(saw_hero);
    REQUIRE(saw_parts);
}
