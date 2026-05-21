#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "../archive_invariants.hpp"
#include "io.hpp"
#include "project_ops.hpp"

#include <nlohmann/json.hpp>

#include <libslic3r/miniz_extension.hpp>

#include <boost/filesystem.hpp>
#include <regex>
#include <set>
#include <string>

using namespace orca_cli_test;

TEST_CASE("orca-cli: object add places STL on named plate with source_file stamped",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-add");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "Brackets"}).exit_code == 0);

    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "Brackets",
                      "--stl",   cube.string(),
                      "--name",  "testcube"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    archive::run_all_basic(in);
    archive::assert_parts_have_source_file(in);

    // Re-load and confirm the object exists.
    auto s = orca_cli::load_project(in.string());
    bool found = false;
    for (auto* obj : s.model->objects)
        if (obj->name == "testcube") found = true;
    REQUIRE(found);
}

TEST_CASE("orca-cli: object add --count 3 creates 3 ModelObjects",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-add-count");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "P"}).exit_code == 0);
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "P",
                      "--stl",   cube.string(),
                      "--count", "3",
                      "--name",  "triple"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    int matches = 0;
    int total_instances = 0;
    for (auto* obj : s.model->objects) {
        if (obj->name != "triple") continue;
        ++matches;
        total_instances += int(obj->instances.size());
    }
    REQUIRE(matches == 3);
    REQUIRE(total_instances == 3);
}

TEST_CASE("orca-cli: object add returns unknown_reference on missing plate",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-add-noplate");
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "Nope",
                      "--stl",   cube.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 6); // unknown_reference
}

TEST_CASE("orca-cli: object add returns file_not_found on missing stl",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-add-nostl");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "X"}).exit_code == 0);
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "X",
                      "--stl",   "C:\\does\\not\\exist.stl"});
    REQUIRE(r.exit_code == 2); // file_not_found
}

TEST_CASE("orca-cli: object remove removes by name",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-remove");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "R"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "R",
                     "--stl",   cube.string(),
                     "--name",  "gone"}).exit_code == 0);
    auto r = run_cli({"object", "remove", in.string(), "--name", "gone"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    bool found = false;
    for (auto* obj : s.model->objects)
        if (obj->name == "gone") found = true;
    REQUIRE_FALSE(found);
}

TEST_CASE("orca-cli: object remove returns unknown_reference on missing name",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-rm-missing");
    auto r = run_cli({"object", "remove", in.string(), "--name", "ghost"});
    REQUIRE(r.exit_code == 6);
}

TEST_CASE("orca-cli: object list rejects --output with usage_error",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-list-out");
    auto r = run_cli({"object", "list", in.string(),
                      "--output", (tmp / "x.3mf").string()});
    REQUIRE(r.exit_code == 1);
}

TEST_CASE("orca-cli: object list lists objects",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-list");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "L"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "L",
                     "--stl",   cube.string(),
                     "--name",  "listed"}).exit_code == 0);
    auto r = run_cli({"object", "list", in.string()});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_.find("listed") != std::string::npos);
}

// ---------------------------------------------------------------------------
// P4 -- object transforms
//
// The transform flags switch add_object from the deterministic grid
// placement of P3 to "stacking" -- all --count N instances share the same
// post-transform offset. Per-plate origin is folded into the world offset
// so --translate is plate-local: (0,0) is the bed-min corner of the named
// plate. These tests pin to plate 0 ("Plate 01 test") in the reference
// 3mf where plate_origin_offset is (0,0), making the world-space assertion
// trivially equal the requested local offset.
// ---------------------------------------------------------------------------

TEST_CASE("orca-cli: object add --translate 60,60 places one instance at (60,60,0)",
          "[orca-cli][P4][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped: reference 3mf not available"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-translate-single");
    // Target plate 0 (origin offset (0,0)) so world == local offset.
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "Plate 01 test",
                      "--stl",   cube.string(),
                      "--translate", "60,60",
                      "--name", "cubeT"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : s.model->objects)
        if (o->name == "cubeT") obj = o;
    REQUIRE(obj != nullptr);
    REQUIRE(obj->instances.size() == 1u);
    REQUIRE_THAT(obj->instances.front()->get_offset().x(),
                 Catch::Matchers::WithinAbs(60.0, 0.001));
    REQUIRE_THAT(obj->instances.front()->get_offset().y(),
                 Catch::Matchers::WithinAbs(60.0, 0.001));
}

TEST_CASE("orca-cli: --count 3 with --translate stacks 3 instances at the same offset",
          "[orca-cli][P4][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-translate-stack");
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "Plate 01 test",
                      "--stl",   cube.string(),
                      "--translate", "60,60",
                      "--count", "3",
                      "--name", "stack"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    std::vector<const Slic3r::ModelObject*> stack_objs;
    for (auto* o : s.model->objects) if (o->name == "stack") stack_objs.push_back(o);
    REQUIRE(stack_objs.size() == 3u);
    // Each ModelObject in the cluster has exactly one instance, and all three
    // share the same world offset (the --translate stack post-transform position).
    const Slic3r::Vec3d off0 = stack_objs[0]->instances.front()->get_offset();
    for (const auto* o : stack_objs) {
        REQUIRE(o->instances.size() == 1u);
        REQUIRE_THAT((off0 - o->instances.front()->get_offset()).norm(),
                     Catch::Matchers::WithinAbs(0.0, 1e-6));
    }
}

TEST_CASE("orca-cli: --count 3 without transforms keeps grid placement",
          "[orca-cli][P4][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-count-grid");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "G"}).exit_code == 0);
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "G",
                      "--stl",   cube.string(),
                      "--count", "3",
                      "--name", "grid"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    std::vector<const Slic3r::ModelObject*> grid_objs;
    for (auto* o : s.model->objects) if (o->name == "grid") grid_objs.push_back(o);
    REQUIRE(grid_objs.size() == 3u);
    // Three distinct grid offsets; assert all unique.
    std::vector<Slic3r::Vec3d> offsets;
    for (const auto* o : grid_objs) {
        REQUIRE(o->instances.size() == 1u);
        offsets.push_back(o->instances.front()->get_offset());
    }
    for (size_t i = 0; i < offsets.size(); ++i)
        for (size_t j = i + 1; j < offsets.size(); ++j)
            REQUIRE((offsets[i] - offsets[j]).norm() > 15.0);   // pairwise > 15 mm apart (well below the 20 mm grid stride for the committed 10 mm cube fixture)
}

TEST_CASE("orca-cli: --translate off-bed returns exit 9 placement_failure",
          "[orca-cli][P4][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-offbed");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "OB"}).exit_code == 0);
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "OB",
                      "--stl",   cube.string(),
                      "--translate", "99999,99999"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 9); // placement_failure
}

TEST_CASE("orca-cli: --scale 2 doubles instance scaling_factor",
          "[orca-cli][P4][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-scale");
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "Plate 01 test",
                      "--stl",   cube.string(),
                      "--translate", "60,60",
                      "--scale", "2",
                      "--name",  "big"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : s.model->objects)
        if (o->name == "big") obj = o;
    REQUIRE(obj != nullptr);
    REQUIRE(obj->instances.size() == 1u);
    auto sf = obj->instances.front()->get_scaling_factor();
    REQUIRE_THAT(sf.x(), Catch::Matchers::WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(sf.y(), Catch::Matchers::WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(sf.z(), Catch::Matchers::WithinAbs(2.0, 1e-9));
}

TEST_CASE("orca-cli: --rotate 0,0,pi/4 sets instance rotation about Z",
          "[orca-cli][P4][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-rotate");
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "Plate 01 test",
                      "--stl",   cube.string(),
                      "--translate", "60,60",
                      "--rotate", "0,0,0.7853981633974483",  // pi/4
                      "--name",   "spun"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : s.model->objects)
        if (o->name == "spun") obj = o;
    REQUIRE(obj != nullptr);
    auto rot = obj->instances.front()->get_rotation();
    REQUIRE_THAT(rot.z(),
                 Catch::Matchers::WithinAbs(0.7853981633974483, 1e-6));
}

TEST_CASE("orca-cli: --translate with bad value returns usage_error",
          "[orca-cli][P4][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-translate-bad");
    auto r = run_cli({"object", "add", in.string(),
                      "--plate", "Plate 01 test",
                      "--stl",   cube.string(),
                      "--translate", "not,a,number"});
    REQUIRE(r.exit_code == 1); // usage_error
}

TEST_CASE("orca-cli: --translate '60,,60' (empty middle token) returns usage_error", "[orca-cli][P4][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-empty-token");
    auto cube = (stl_dir() / "000_01_test_cube.stl").string();
    auto r = run_cli({"object","add",in.string(),"--plate","Plate 01 test","--stl",cube,
                      "--translate","60,,60"});
    REQUIRE(r.exit_code == 1);  // usage_error
}

TEST_CASE("orca-cli: --scale '1,2' (2-component) returns usage_error", "[orca-cli][P4][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-scale-2comp");
    auto cube = (stl_dir() / "000_01_test_cube.stl").string();
    auto r = run_cli({"object","add",in.string(),"--plate","Plate 01 test","--stl",cube,
                      "--scale","1,2"});
    REQUIRE(r.exit_code == 1);
}

TEST_CASE("orca-cli: --scale '1' (uniform scalar) succeeds", "[orca-cli][P4][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-scale-1");
    auto cube = (stl_dir() / "000_01_test_cube.stl").string();
    // --translate "60,60" is supplied to keep the AABB inside the plate
    // (matches the pattern of the existing --scale 2 P4 test); the point
    // here is that the uniform-scalar arity is still accepted post-fix.
    auto r = run_cli({"object","add",in.string(),"--plate","Plate 01 test","--stl",cube,
                      "--translate","60,60","--scale","1","--name","unit"});
    REQUIRE(r.exit_code == 0);
}

// ---------------------------------------------------------------------------
// P5 -- object filaments
//
// `--filament N` on `object add` stamps `extruder = N` on the new
// ModelObject's per-object config. `object set-filament --name M
// --filament N` retroactively assigns the slot. Slot validation:
// N must be in [1, filament_settings_id.size()]. The reference 3mf
// has 6 slots, so --filament 1..6 are valid; --filament 7+ are out
// of range.
//
// Bug C regression lock-in: when --filament writes an extruder on
// the new object, the source_file attribution on every <part> must
// still be present. v1.2 historically lost source_file in this path
// and the GUI silently dropped the resulting objects on open.
// ---------------------------------------------------------------------------

TEST_CASE("orca-cli: object add --filament 2 stamps extruder=2 and preserves source_file",
          "[orca-cli][P5][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-filament");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "F"}).exit_code == 0);
    auto r = run_cli({"object", "add", in.string(),
                      "--plate",    "F",
                      "--stl",      cube.string(),
                      "--filament", "2",
                      "--name",     "cube_f2"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);

    archive::assert_object_extruder(in, "cube_f2", 2);
    // Bug C regression lock-in: source_file presence must hold even
    // when --filament writes per-object extruder. v1.2 historically
    // lost source_file on filament-assigned objects and the GUI
    // silently dropped them on open.
    archive::assert_parts_have_source_file(in);
}

TEST_CASE("orca-cli: object add --filament survives load/save roundtrip",
          "[orca-cli][P5][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-filament-roundtrip");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "RT"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate",    "RT",
                     "--stl",      cube.string(),
                     "--filament", "4",
                     "--name",     "rtcube"}).exit_code == 0);

    // Re-load the saved archive and confirm the per-object extruder
    // survived. Tests that ModelConfigObject's serialization path in
    // bbs_3mf emits `<metadata key="extruder" value="N"/>` and the
    // loader reads it back into obj->config.
    auto s = orca_cli::load_project(in.string());
    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : s.model->objects)
        if (o->name == "rtcube") obj = o;
    REQUIRE(obj != nullptr);
    REQUIRE(obj->config.has("extruder"));
    REQUIRE(obj->config.opt_int("extruder") == 4);
}

TEST_CASE("orca-cli: object add --filament 99 returns unknown_reference",
          "[orca-cli][P5][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-filament-bad");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "F"}).exit_code == 0);
    auto r = run_cli({"object", "add", in.string(),
                      "--plate",    "F",
                      "--stl",      cube.string(),
                      "--filament", "99",
                      "--name",     "cube"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 6); // unknown_reference
}

TEST_CASE("orca-cli: object set-filament updates extruder retroactively",
          "[orca-cli][P5][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-set-filament");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "F"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "F",
                     "--stl",   cube.string(),
                     "--name",  "retro"}).exit_code == 0);

    auto r = run_cli({"object", "set-filament", in.string(),
                      "--name",     "retro",
                      "--filament", "3"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    archive::assert_object_extruder(in, "retro", 3);
}

TEST_CASE("orca-cli: object set-filament out-of-range returns unknown_reference",
          "[orca-cli][P5][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-set-filament-bad");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "F"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "F",
                     "--stl",   cube.string(),
                     "--name",  "cube"}).exit_code == 0);
    auto r = run_cli({"object", "set-filament", in.string(),
                      "--name",     "cube",
                      "--filament", "99"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 6); // unknown_reference
}

TEST_CASE("orca-cli: object set-filament unknown object returns unknown_reference",
          "[orca-cli][P5][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-set-filament-ghost");
    auto r = run_cli({"object", "set-filament", in.string(),
                      "--name",     "ghost",
                      "--filament", "1"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 6);
}

TEST_CASE("orca-cli: object list reports the plate name for objects on plates", "[orca-cli][P3][e2e]") {
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-list-plate");
    REQUIRE(run_cli({"plate","add",in.string(),"--name","Listed"}).exit_code == 0);
    REQUIRE(run_cli({"object","add",in.string(),"--plate","Listed","--stl",cube.string(),
                     "--name","listobj"}).exit_code == 0);

    auto r = run_cli({"--json","object","list",in.string()});
    REQUIRE(r.exit_code == 0);
    INFO(r.stdout_);
    auto j = nlohmann::json::parse(r.stdout_);
    REQUIRE(j["status"] == "ok");
    bool found_obj = false;
    for (const auto& obj : j["data"]["objects"]) {
        if (obj["name"] == "listobj" && obj["plate"] == "Listed") {
            found_obj = true;
            break;
        }
    }
    REQUIRE(found_obj);
}

TEST_CASE("orca-cli: object list --plate P filters to objects on the named plate",
          "[orca-cli][P3][e2e]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "obj-list-filter");
    REQUIRE(run_cli({"plate","add",in.string(),"--name","A"}).exit_code == 0);
    REQUIRE(run_cli({"plate","add",in.string(),"--name","B"}).exit_code == 0);
    REQUIRE(run_cli({"object","add",in.string(),"--plate","A","--stl",cube.string(),
                     "--name","onA"}).exit_code == 0);
    REQUIRE(run_cli({"object","add",in.string(),"--plate","B","--stl",cube.string(),
                     "--name","onB"}).exit_code == 0);

    // Filter to plate A: only "onA" should appear.
    auto r = run_cli({"--json","object","list",in.string(),"--plate","A"});
    INFO("stdout: " << r.stdout_ << "\nstderr: " << r.stderr_);
    REQUIRE(r.exit_code == 0);
    auto j = nlohmann::json::parse(r.stdout_);
    REQUIRE(j["status"] == "ok");
    bool seen_onA = false, seen_onB = false;
    for (const auto& obj : j["data"]["objects"]) {
        if (obj["name"] == "onA") seen_onA = true;
        if (obj["name"] == "onB") seen_onB = true;
    }
    REQUIRE(seen_onA);
    REQUIRE_FALSE(seen_onB);

    // Unknown plate: exit 6 (unknown_reference).
    auto r2 = run_cli({"object","list",in.string(),"--plate","NoSuchPlate"});
    INFO("stdout: " << r2.stdout_ << "\nstderr: " << r2.stderr_);
    REQUIRE(r2.exit_code == 6);
}

// 2026-05-21 cross-project audit v2, item 1: silent data-loss regression.
// `obj_inst_map.emplace(object_id, ...)` at src/libslic3r/Format/bbs_3mf.cpp:4697
// collapses N <plater_instance> entries into one on reload, so a single-object-
// with-N-instances model loses N-1 instances on every save+load round-trip.
// This test asserts the saved 3mf survives a *second* save (load -> trivial
// mutation -> save) with all three units of the --count 3 group still present.
TEST_CASE("orca-cli: --count 3 survives load+save roundtrip",
          "[orca-cli][P3][e2e][count-roundtrip]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "count-roundtrip");

    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "P"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "P",
                     "--stl",   cube.string(),
                     "--count", "3",
                     "--name",  "C"}).exit_code == 0);
    // Trivial second mutation forces a load+save cycle.
    REQUIRE(run_cli({"plate", "rename", in.string(),
                     "--from", "P", "--to", "Pp"}).exit_code == 0);

    auto s = orca_cli::load_project(in.string());

    // Group semantics: --count 3 produces 3 distinct ModelObjects named "C",
    // each with exactly 1 ModelInstance.
    int objects_named_c = 0;
    int instance_total  = 0;
    for (const auto* obj : s.model->objects) {
        if (obj->name == "C") {
            ++objects_named_c;
            instance_total += int(obj->instances.size());
        }
    }
    REQUIRE(objects_named_c == 3);
    REQUIRE(instance_total  == 3);

    // The plate must reference all 3 instances after the round-trip (this is
    // the bug-detecting assertion -- pre-fix this drops to 1).
    int plate_entries_for_c = 0;
    for (const auto& pd : s.plates) {
        if (pd->plate_name != "Pp") continue;
        for (const auto& kv : pd->objects_and_instances) {
            const Slic3r::ModelObject* obj = s.model->objects[size_t(kv.first)];
            if (obj->name == "C") ++plate_entries_for_c;
        }
    }
    REQUIRE(plate_entries_for_c == 3);
}

// 2026-05-21 cross-project audit v2, item 1 cont'd: --count N produces N
// independent ModelObjects sharing one name. `object remove --name X` must
// therefore remove ALL of them (group-by-name), otherwise the user would
// have to issue one remove per copy.
TEST_CASE("orca-cli: object remove with --count N removes every clone",
          "[orca-cli][P3][e2e][group-remove]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "group-remove");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "R"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "R",
                     "--stl",   cube.string(),
                     "--count", "3",
                     "--name",  "trio"}).exit_code == 0);
    REQUIRE(run_cli({"object", "remove", in.string(),
                     "--name", "trio"}).exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    int remaining = 0;
    for (const auto* o : s.model->objects)
        if (o->name == "trio") ++remaining;
    REQUIRE(remaining == 0);
}

// 2026-05-21 cross-project audit v2, item 1 cont'd: object set-filament must
// be group-by-name so every clone created by --count N picks up the same slot.
TEST_CASE("orca-cli: object set-filament with --count N stamps every clone",
          "[orca-cli][P5][e2e][group-set-filament]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "group-set-filament");
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "F"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "F",
                     "--stl",   cube.string(),
                     "--count", "3",
                     "--name",  "trio_f"}).exit_code == 0);
    REQUIRE(run_cli({"object", "set-filament", in.string(),
                     "--name", "trio_f",
                     "--filament", "2"}).exit_code == 0);

    auto s = orca_cli::load_project(in.string());
    int matches = 0;
    for (const auto* o : s.model->objects) {
        if (o->name != "trio_f") continue;
        ++matches;
        const auto* eopt = o->config.get().opt<Slic3r::ConfigOptionInt>("extruder");
        REQUIRE(eopt != nullptr);
        REQUIRE(eopt->value == 2);
    }
    REQUIRE(matches == 3);
}

// 2026-05-21 cross-project audit v2, item 2: identify_id collision regression.
// Adding one object to each of two freshly-empty plates must produce two
// distinct identify_ids in Metadata/model_settings.config. Sibling shipped
// faa5f4dd6 to fix per-plate identify_id allocation; this side never sets
// loaded_id and relies on libslic3r's global ObjectBase counter -- this test
// pins that behavior against any future regression.
TEST_CASE("orca-cli: identify_id stays unique across freshly-empty plates",
          "[orca-cli][P3][e2e][cross_plate]")
{
    if (ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto cube = (stl_dir() / "000_01_test_cube.stl");
    if (!fs::exists(cube)) { SUCCEED("Skipped"); return; }

    auto tmp = make_temp_dir();
    auto in  = copy_ref_to_temp(tmp, "cross-plate-id");

    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "A"}).exit_code == 0);
    REQUIRE(run_cli({"plate", "add", in.string(), "--name", "B"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "A",
                     "--stl",   cube.string(),
                     "--name",  "objA"}).exit_code == 0);
    REQUIRE(run_cli({"object", "add", in.string(),
                     "--plate", "B",
                     "--stl",   cube.string(),
                     "--name",  "objB"}).exit_code == 0);

    // Unzip the saved 3mf and scrape every identify_id value from
    // Metadata/model_settings.config; require all values are distinct.
    mz_zip_archive zip{};
    REQUIRE(Slic3r::open_zip_reader(&zip, in.string()));
    int idx = mz_zip_reader_locate_file(&zip, "Metadata/model_settings.config", nullptr, 0);
    REQUIRE(idx >= 0);
    size_t size = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip, mz_uint(idx), &size, 0);
    REQUIRE(p != nullptr);
    std::string contents(reinterpret_cast<const char*>(p), size);
    mz_free(p);
    Slic3r::close_zip_reader(&zip);

    // Pull every value="N" that follows key="identify_id".
    std::regex id_re("key=\"identify_id\"\\s+value=\"(\\d+)\"");
    std::set<std::string> ids;
    auto it = std::sregex_iterator(contents.begin(), contents.end(), id_re);
    auto end = std::sregex_iterator();
    int total = 0;
    for (; it != end; ++it) { ids.insert((*it)[1].str()); ++total; }
    INFO("found " << total << " identify_id entries, " << ids.size() << " distinct");
    REQUIRE(total > 0);
    REQUIRE(ids.size() == size_t(total));
}
