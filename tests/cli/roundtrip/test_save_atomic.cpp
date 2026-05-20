#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "io.hpp"
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
using namespace orca_cli;
using namespace orca_cli_test;

TEST_CASE("save_project leaves destination present and cleans up scratch files",
          "[orca-cli][cleanup][T11]") {
    if (ref_3mf().empty()) {
        SUCCEED("Skipped: reference 3mf not on host");
        return;
    }

    auto tmp_dir = make_temp_dir();
    const auto dst = tmp_dir / "out.3mf";
    fs::copy_file(ref_3mf(), dst);

    auto state = load_project(dst.string());
    REQUIRE_NOTHROW(save_project(state, dst.string()));
    REQUIRE(fs::exists(dst));
    REQUIRE_FALSE(fs::exists(dst.string() + ".tmp"));
    REQUIRE_FALSE(fs::exists(dst.string() + ".bak"));
    REQUIRE_FALSE(fs::exists(dst.string() + ".rewrite"));

    fs::remove_all(tmp_dir);
}
