// OrcaSlicer vendors Catch2 v3; umbrella header is catch_all.hpp.
#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "../archive_invariants.hpp"
#include <libslic3r/miniz_extension.hpp>

using namespace orca_cli_test;

TEST_CASE("orca-cli: project init clones reference and passes archive invariants",
          "[orca-cli][P1][e2e]") {
    if (ref_3mf().empty()) {
        SUCCEED("Skipped: reference 3mf not on host");
        return;
    }
    auto tmp = make_temp_dir();
    auto out = (tmp / "p1.3mf").string();

    auto r = run_cli({"project", "init", out, "--template", ref_3mf().string()});
    INFO("stdout: " << r.stdout_);
    INFO("stderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    archive::run_all_basic(out);
    archive::assert_printable_area_4_points(out);
    archive::assert_parts_have_source_file(out);
}

TEST_CASE("orca-cli: project init returns file_not_found on missing template",
          "[orca-cli][P1][e2e]") {
    auto tmp = make_temp_dir();
    auto out = (tmp / "x.3mf").string();
    auto r = run_cli({"project", "init", out, "--template",
                      "C:\\does\\not\\exist.3mf"});
    INFO("stdout: " << r.stdout_);
    INFO("stderr: " << r.stderr_);
    REQUIRE(r.exit_code == 2);
}

// Cross-project audit P2: a broken input template must fail at project init
// time, not be silently auto-fixed by save_project's placeholder thumbnail
// passthrough. We construct a broken template by copying the reference 3mf
// and deleting plate_1_small.png from it, then assert orca-cli exits 8.
TEST_CASE("orca-cli: project init exits 8 on input template with missing plate thumbnail",
          "[orca-cli][P1][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto broken = tmp / "broken_template.3mf";
    fs::copy_file(ref_3mf(), broken, fs::copy_options::overwrite_existing);

    // Strip plate_1_small.png from the copied template. We rewrite the
    // archive with all entries EXCEPT that one.
    {
        mz_zip_archive in{};
        REQUIRE(Slic3r::open_zip_reader(&in, broken.string()));
        auto stripped = tmp / "broken_stripped.3mf";
        mz_zip_archive out{};
        REQUIRE(Slic3r::open_zip_writer(&out, stripped.string()));

        const mz_uint n = mz_zip_reader_get_num_files(&in);
        for (mz_uint i = 0; i < n; ++i) {
            mz_zip_archive_file_stat st;
            REQUIRE(mz_zip_reader_file_stat(&in, i, &st));
            std::string name = st.m_filename;
            std::replace(name.begin(), name.end(), '\\', '/');
            if (name == "Metadata/plate_1_small.png") continue;
            REQUIRE(mz_zip_writer_add_from_zip_reader(&out, &in, i));
        }
        REQUIRE(mz_zip_writer_finalize_archive(&out));
        Slic3r::close_zip_writer(&out);
        Slic3r::close_zip_reader(&in);
        fs::remove(broken);
        fs::rename(stripped, broken);
    }

    auto out_path = tmp / "init_should_fail.3mf";
    auto r = run_cli({"project", "init", out_path.string(),
                      "--template", broken.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 8);
    // Output should NOT exist - the failure must surface before save_project
    // would have auto-fixed it via placeholder passthrough.
    REQUIRE_FALSE(fs::exists(out_path));
}
