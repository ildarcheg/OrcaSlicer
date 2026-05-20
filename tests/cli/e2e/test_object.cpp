#include <catch2/catch_all.hpp>
#include "../test_common.hpp"
#include "../archive_invariants.hpp"
#include "io.hpp"
#include "project_ops.hpp"

#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>
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

TEST_CASE("orca-cli: object add --count 3 creates 3 instances",
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
    int total_instances = 0;
    for (auto* obj : s.model->objects)
        if (obj->name == "triple") total_instances = int(obj->instances.size());
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
    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : s.model->objects)
        if (o->name == "stack") obj = o;
    REQUIRE(obj != nullptr);
    REQUIRE(obj->instances.size() == 3u);
    for (auto* inst : obj->instances) {
        REQUIRE_THAT(inst->get_offset().x(),
                     Catch::Matchers::WithinAbs(60.0, 0.001));
        REQUIRE_THAT(inst->get_offset().y(),
                     Catch::Matchers::WithinAbs(60.0, 0.001));
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
    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : s.model->objects)
        if (o->name == "grid") obj = o;
    REQUIRE(obj != nullptr);
    REQUIRE(obj->instances.size() == 3u);
    // No two instances share the same offset (grid placement is distinct
    // per slot). Pairwise norm check defends against the placement.cpp
    // collision bug captured in tests/cli/unit/test_placement.cpp.
    auto p0 = obj->instances[0]->get_offset();
    auto p1 = obj->instances[1]->get_offset();
    auto p2 = obj->instances[2]->get_offset();
    REQUIRE_FALSE((p0 - p1).norm() < 1.0);
    REQUIRE_FALSE((p0 - p2).norm() < 1.0);
    REQUIRE_FALSE((p1 - p2).norm() < 1.0);
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
