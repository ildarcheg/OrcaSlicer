// tests/cli/roundtrip/test_project_tab.cpp
// Roundtrip tests for project-tab edits (spec § 6.3).
// (a) full set of info/profile/aux fields survive pack/unpack
// (b) --cover bytes + pointer survive
// (c) aux add+remove leaves bucket empty
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "io.hpp"
#include "project_ops.hpp"
#include "project_tab_ops.hpp"

#include <boost/filesystem.hpp>
#include <fstream>

using namespace orca_cli;
using namespace orca_cli_test;
namespace fs = boost::filesystem;

static std::vector<unsigned char> read_all(const fs::path& p) {
    std::ifstream f(p.string(), std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),
                                      std::istreambuf_iterator<char>{});
}

TEST_CASE("orca-cli: project info+profile+aux survive save/load roundtrip",
          "[orca-cli][project-tab][roundtrip]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "ptab-rt");
    REQUIRE(run_cli({"project","info","set",in.string(),
                     "--title","RT","--description","D","--license","MIT"}).exit_code == 0);
    REQUIRE(run_cli({"project","profile","set",in.string(),
                     "--title","RPT","--description","RPD"}).exit_code == 0);
    REQUIRE(run_cli({"project","aux","add",in.string(),
                     "--folder","others","--file",cube.string(),"--name","x.bin"}).exit_code == 0);

    auto s = load_project(in.string());
    auto iv = info_view(s);
    auto pv = profile_view(s);
    REQUIRE(iv.title       == "RT");
    REQUIRE(iv.description == "D");
    REQUIRE(iv.license     == "MIT");
    REQUIRE(pv.title       == "RPT");
    REQUIRE(pv.description == "RPD");
    auto entries = aux_list(s);
    bool saw = false;
    for (const auto& e : entries)
        if (e.folder == AuxFolder::others && e.name == "x.bin") saw = true;
    REQUIRE(saw);
}

TEST_CASE("orca-cli: project info set --cover survives save/load (pointer + bytes)",
          "[orca-cli][project-tab][roundtrip]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto png = fs::path(ORCA_CLI_FIXTURES_DIR) / "cover_smoke.png";
    if (!fs::exists(png)) { SUCCEED("Skipped: cover_smoke.png missing"); return; }
    auto src_bytes = read_all(png);
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "ptab-cover-rt");

    REQUIRE(run_cli({"project","info","set",in.string(),"--cover",png.string()}).exit_code == 0);

    auto s = load_project(in.string());
    REQUIRE(s.model->model_info != nullptr);
    REQUIRE(s.model->model_info->cover_file
            == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    auto landed = fs::path(s.model->get_auxiliary_file_temp_path())
                  / ".thumbnails" / "thumbnail_3mf.png";
    REQUIRE(fs::exists(landed));
    REQUIRE(read_all(landed) == src_bytes);
}

TEST_CASE("orca-cli: project aux add then remove leaves bucket empty after roundtrip",
          "[orca-cli][project-tab][roundtrip]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = stl_dir() / "000_01_test_cube.stl";
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "ptab-aux-rt");
    REQUIRE(run_cli({"project","aux","add",in.string(),"--folder","pictures","--file",cube.string(),"--name","x.png"}).exit_code == 0);
    REQUIRE(run_cli({"project","aux","remove",in.string(),"--folder","pictures","--name","x.png"}).exit_code == 0);

    auto s = load_project(in.string());
    auto entries = aux_list(s);
    for (const auto& e : entries)
        REQUIRE(!(e.folder == AuxFolder::pictures && e.name == "x.png"));
}
