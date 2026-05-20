#include <catch2/catch_all.hpp>
#include "project_ops.hpp"
#include "io.hpp"
#include "output.hpp"
#include "../test_common.hpp"

#include <boost/filesystem.hpp>

#include <stdexcept>

using namespace orca_cli;

TEST_CASE("orca-cli: add_plate appends plate with given name",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());

    add_plate(s, "Brackets");
    REQUIRE(s.plates.size() == 2u);
    REQUIRE(s.plates.back()->plate_name == "Brackets");
}

TEST_CASE("orca-cli: add_plate rejects duplicate name",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Existing";

    REQUIRE_THROWS_AS(add_plate(s, "Existing"), std::invalid_argument);
}

TEST_CASE("orca-cli: remove_plate removes by name and re-indexes",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    for (int i = 0; i < 3; ++i) {
        s.plates.push_back(std::make_unique<Slic3r::PlateData>());
        s.plates.back()->plate_name  = "P" + std::to_string(i);
        s.plates.back()->plate_index = i;
    }
    remove_plate(s, "P1");
    REQUIRE(s.plates.size() == 2u);
    REQUIRE(s.plates[0]->plate_name == "P0");
    REQUIRE(s.plates[1]->plate_name == "P2");
    REQUIRE(s.plates[1]->plate_index == 1); // re-indexed from 2 to 1
}

TEST_CASE("orca-cli: remove_plate rejects unknown name",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Only";
    // Need >=2 plates to reach the lookup branch; otherwise we hit the
    // invalid_state guard first.
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Other";

    REQUIRE_THROWS_AS(remove_plate(s, "Missing"), std::out_of_range);
}

TEST_CASE("orca-cli: remove_plate refuses to remove only plate",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Only";

    REQUIRE_THROWS_AS(remove_plate(s, "Only"), std::invalid_argument);
}

TEST_CASE("orca-cli: rename_plate updates name only, no re-index",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    for (int i = 0; i < 2; ++i) {
        s.plates.push_back(std::make_unique<Slic3r::PlateData>());
        s.plates.back()->plate_name  = "P" + std::to_string(i);
        s.plates.back()->plate_index = i;
    }
    rename_plate(s, "P0", "Renamed");
    REQUIRE(s.plates[0]->plate_name  == "Renamed");
    REQUIRE(s.plates[0]->plate_index == 0); // unchanged
    REQUIRE(s.plates[1]->plate_index == 1);
}

TEST_CASE("orca-cli: rename_plate rejects unknown from",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "A";

    REQUIRE_THROWS_AS(rename_plate(s, "Missing", "X"), std::out_of_range);
}

TEST_CASE("orca-cli: rename_plate rejects duplicate to",
          "[orca-cli][P2][unit]")
{
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    for (auto& n : {"A", "B"}) {
        s.plates.push_back(std::make_unique<Slic3r::PlateData>());
        s.plates.back()->plate_name = n;
    }
    REQUIRE_THROWS_AS(rename_plate(s, "A", "B"), std::invalid_argument);
}

TEST_CASE("orca-cli: rename_plate self-rename succeeds if from exists", "[orca-cli][P2][unit]") {
    using namespace orca_cli;
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Same";
    REQUIRE_NOTHROW(rename_plate(s, "Same", "Same"));
}

TEST_CASE("orca-cli: rename_plate self-rename rejects unknown from", "[orca-cli][P2][unit]") {
    using namespace orca_cli;
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    s.plates.back()->plate_name = "Exists";
    REQUIRE_THROWS_AS(rename_plate(s, "Missing", "Missing"), std::out_of_range);
}

// -- P3: add_object / remove_object ----------------------------------------

TEST_CASE("orca-cli: add_object loads STL, stamps source attribution, places on plate",
          "[orca-cli][P3][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped: no cube fixture"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "P3Test");

    const size_t objects_before = s.model->objects.size();

    AddObjectParams p;
    p.plate_name  = "P3Test";
    p.stl_path    = stl.string();
    p.object_name = "cube";
    p.count       = 1;
    add_object(s, p);

    REQUIRE(s.model->objects.size() == objects_before + 1u);
    auto* obj = s.model->objects.back();
    REQUIRE(obj->name == "cube");
    REQUIRE_FALSE(obj->volumes.empty());
    // Bug C defense: every volume's Source must be stamped.
    REQUIRE(obj->volumes.front()->source.input_file == stl.string());
    REQUIRE(obj->volumes.front()->source.object_idx == 0);
    REQUIRE(obj->volumes.front()->source.volume_idx == 0);
    REQUIRE(obj->instances.size() == 1u);

    // The new plate must reference at least one instance after the add.
    bool found_on_plate = false;
    for (auto& pd : s.plates) {
        if (pd->plate_name != "P3Test") continue;
        if (!pd->objects_and_instances.empty()) found_on_plate = true;
    }
    REQUIRE(found_on_plate);
}

TEST_CASE("orca-cli: add_object with count=3 creates 3 instances",
          "[orca-cli][P3][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "P3Count");

    AddObjectParams p;
    p.plate_name = "P3Count";
    p.stl_path   = stl.string();
    p.count      = 3;
    add_object(s, p);

    auto* obj = s.model->objects.back();
    REQUIRE(obj->instances.size() == 3u);
}

TEST_CASE("orca-cli: add_object rejects unknown plate",
          "[orca-cli][P3][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());

    AddObjectParams p;
    p.plate_name = "DoesNotExist";
    p.stl_path   = stl.string();
    REQUIRE_THROWS_AS(add_object(s, p), std::out_of_range);
}

TEST_CASE("orca-cli: remove_object removes by name and rebuilds plate map",
          "[orca-cli][P3][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "P3Rm");
    AddObjectParams p;
    p.plate_name  = "P3Rm";
    p.stl_path    = stl.string();
    p.object_name = "victim";
    add_object(s, p);
    const size_t n_before = s.model->objects.size();

    remove_object(s, "victim");
    REQUIRE(s.model->objects.size() == n_before - 1u);

    // Map must no longer reference the removed object.
    bool referenced = false;
    for (auto& pd : s.plates) {
        for (const auto& kv : pd->objects_and_instances) {
            if (kv.first < 0 || kv.first >= int(s.model->objects.size())) {
                referenced = true; // dangling -> bug
            }
        }
    }
    REQUIRE_FALSE(referenced);
}

TEST_CASE("orca-cli: remove_object rejects unknown name",
          "[orca-cli][P3][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    REQUIRE_THROWS_AS(remove_object(s, "ghost-does-not-exist"),
                      std::out_of_range);
}

// -- P5: set_object_filament -----------------------------------------------

TEST_CASE("orca-cli: set_object_filament writes extruder on per-object config",
          "[orca-cli][P5][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "F5Test");

    AddObjectParams p;
    p.plate_name  = "F5Test";
    p.stl_path    = stl.string();
    p.object_name = "fcube";
    add_object(s, p);

    set_object_filament(s, "fcube", 2);

    // Verify by reading the object's config directly. The reference 3mf
    // has 6 filament slots (Bambu PLA Basic @BBL A1 x6), so slot 2 is
    // safely in range.
    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : s.model->objects)
        if (o->name == "fcube") obj = o;
    REQUIRE(obj != nullptr);
    REQUIRE(obj->config.has("extruder"));
    const auto* opt = obj->config.option("extruder");
    REQUIRE(opt != nullptr);
    REQUIRE(obj->config.opt_int("extruder") == 2);
}

TEST_CASE("orca-cli: set_object_filament rejects out-of-range slot",
          "[orca-cli][P5][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "F5Test2");

    AddObjectParams p;
    p.plate_name  = "F5Test2";
    p.stl_path    = stl.string();
    p.object_name = "fc";
    add_object(s, p);

    // Reference 3mf has 6 slots; 99 and 0 are both out of range. Slot 7
    // (just past the high end) is also asserted, to defend against an
    // off-by-one in the upper bound.
    REQUIRE_THROWS_AS(set_object_filament(s, "fc", 99), std::out_of_range);
    REQUIRE_THROWS_AS(set_object_filament(s, "fc", 7),  std::out_of_range);
    REQUIRE_THROWS_AS(set_object_filament(s, "fc", 0),  std::out_of_range);
    REQUIRE_THROWS_AS(set_object_filament(s, "fc", -1), std::out_of_range);
}

TEST_CASE("orca-cli: set_object_filament rejects unknown object",
          "[orca-cli][P5][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    REQUIRE_THROWS_AS(set_object_filament(s, "ghost", 1), std::out_of_range);
}

TEST_CASE("orca-cli: add_object with filament_slot stamps extruder",
          "[orca-cli][P5][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "F5Add");

    AddObjectParams p;
    p.plate_name    = "F5Add";
    p.stl_path      = stl.string();
    p.object_name   = "fadd";
    p.filament_slot = 3;
    add_object(s, p);

    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : s.model->objects)
        if (o->name == "fadd") obj = o;
    REQUIRE(obj != nullptr);
    REQUIRE(obj->config.has("extruder"));
    REQUIRE(obj->config.opt_int("extruder") == 3);
}

TEST_CASE("orca-cli: add_object with out-of-range filament_slot throws",
          "[orca-cli][P5][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "F5AddBad");

    AddObjectParams p;
    p.plate_name    = "F5AddBad";
    p.stl_path      = stl.string();
    p.object_name   = "fbad";
    p.filament_slot = 99;
    REQUIRE_THROWS_AS(add_object(s, p), std::out_of_range);
}

// -- P6: config set / unset / list -----------------------------------------

TEST_CASE("orca-cli: set_project_config writes key with value validation",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    set_project_config(s, "sparse_infill_density", "30%");
    const auto* opt = s.project_config->option("sparse_infill_density");
    REQUIRE(opt != nullptr);
    REQUIRE(opt->serialize() == "30%");
}

TEST_CASE("orca-cli: set_project_config rejects unknown key",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    REQUIRE_THROWS_AS(set_project_config(s, "no_such_key", "42"), BadConfigError);
}

TEST_CASE("orca-cli: set_project_config rejects bad value",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    REQUIRE_THROWS_AS(set_project_config(s, "layer_height", "not-a-number"),
                      BadConfigError);
}

TEST_CASE("orca-cli: set_object_config writes per-object key",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "P6T");

    AddObjectParams p;
    p.plate_name  = "P6T";
    p.stl_path    = stl.string();
    p.object_name = "objc";
    add_object(s, p);

    set_object_config(s, "objc", "wall_loops", "4");

    Slic3r::ModelObject* obj = nullptr;
    for (auto* o : s.model->objects)
        if (o->name == "objc") obj = o;
    REQUIRE(obj != nullptr);
    REQUIRE(obj->config.has("wall_loops"));
    // ModelConfig::option is non-template (returns const ConfigOption*),
    // so we read the value via opt_int rather than a typed option<T>.
    REQUIRE(obj->config.opt_int("wall_loops") == 4);
}

TEST_CASE("orca-cli: set_object_config rejects unknown object",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    REQUIRE_THROWS_AS(set_object_config(s, "ghost", "wall_loops", "4"),
                      std::out_of_range);
}

TEST_CASE("orca-cli: set_object_config rejects unknown key",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped"); return; }

    auto s = load_project(orca_cli_test::ref_3mf().string());
    add_plate(s, "P6TBadKey");

    AddObjectParams p;
    p.plate_name  = "P6TBadKey";
    p.stl_path    = stl.string();
    p.object_name = "obk";
    add_object(s, p);

    // Unknown key must surface as BadConfigError BEFORE the object-not-found
    // check (validate_key_exists runs first in set_object_config).
    REQUIRE_THROWS_AS(set_object_config(s, "obk", "no_such_key", "1"),
                      BadConfigError);
}

TEST_CASE("orca-cli: unset_project_config removes the key",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    set_project_config(s, "sparse_infill_density", "30%");
    REQUIRE(s.project_config->has("sparse_infill_density"));
    unset_project_config(s, "sparse_infill_density");
    REQUIRE_FALSE(s.project_config->has("sparse_infill_density"));
}

TEST_CASE("orca-cli: unset_project_config rejects unknown key",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    REQUIRE_THROWS_AS(unset_project_config(s, "no_such_key"), BadConfigError);
}

TEST_CASE("orca-cli: unset_object_config rejects unknown object",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    // Key must validate first; pick a known-good key so we land on the
    // object-lookup branch.
    REQUIRE_THROWS_AS(unset_object_config(s, "ghost", "wall_loops"),
                      std::out_of_range);
}

TEST_CASE("orca-cli: changed_project_keys returns at least one key for the reference 3mf",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    auto changed = changed_project_keys(s);
    // The reference 3mf has many non-default keys (printer profile,
    // filament settings, custom gcode, ...); the diff must surface at
    // least one of them. This is the G6 smoke check -- if the diff path
    // were going through default_value->serialize() we'd crash on a
    // coEnum here instead of returning a vector.
    REQUIRE_FALSE(changed.empty());
}

TEST_CASE("orca-cli: set_project_config marks the key in different_settings_to_system",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    using namespace orca_cli;
    using namespace Slic3r;
    auto s = load_project(orca_cli_test::ref_3mf().string());

    set_project_config(s, "sparse_infill_density", "30%");

    auto* diff = s.project_config->option<ConfigOptionStrings>("different_settings_to_system");
    REQUIRE(diff != nullptr);
    REQUIRE(diff->values.size() >= 2);
    std::vector<std::string> process_dirty;
    unescape_strings_cstyle(diff->values[0], process_dirty);
    REQUIRE(std::find(process_dirty.begin(), process_dirty.end(), "sparse_infill_density")
            != process_dirty.end());
}

TEST_CASE("orca-cli: unset_project_config clears the key from different_settings_to_system",
          "[orca-cli][P6][unit]")
{
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped"); return; }
    using namespace orca_cli;
    using namespace Slic3r;
    auto s = load_project(orca_cli_test::ref_3mf().string());

    set_project_config(s, "sparse_infill_density", "30%");
    unset_project_config(s, "sparse_infill_density");

    auto* diff = s.project_config->option<ConfigOptionStrings>("different_settings_to_system");
    if (diff) {
        std::vector<std::string> process_dirty;
        unescape_strings_cstyle(diff->values[0], process_dirty);
        REQUIRE(std::find(process_dirty.begin(), process_dirty.end(), "sparse_infill_density")
                == process_dirty.end());
    }
}

// -- T1: check_input_exists (M3) -------------------------------------------

TEST_CASE("check_input_exists returns ok for existing file", "[orca-cli][cleanup][T1]") {
    orca_cli::GlobalOpts g;
    g.json = false;
    const std::string ref = ORCA_CLI_REF_3MF;
    int rc = orca_cli::check_input_exists(g, ref);
    REQUIRE(rc == int(orca_cli::ExitCode::ok));
}

TEST_CASE("check_input_exists returns file_not_found for missing path", "[orca-cli][cleanup][T1]") {
    orca_cli::GlobalOpts g;
    g.json = false;
    int rc = orca_cli::check_input_exists(g, "C:/this/path/does/not/exist.3mf");
    REQUIRE(rc == int(orca_cli::ExitCode::file_not_found));
}

// -- T2: find_object (M9) --------------------------------------------------

TEST_CASE("find_object returns nullptr when missing", "[orca-cli][cleanup][T2]") {
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    REQUIRE(find_object(s, "__nope__") == nullptr);
}

TEST_CASE("find_object_or_throw throws on missing", "[orca-cli][cleanup][T2]") {
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    REQUIRE_THROWS_AS(find_object_or_throw(s, "__nope__"), std::out_of_range);
}

TEST_CASE("filament_slot_count >= 1 on reference", "[orca-cli][cleanup][T3]") {
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    REQUIRE(filament_slot_count(*s.project_config) >= 1);
}

TEST_CASE("printable_area read produces same bed extent before/after refactor",
          "[orca-cli][cleanup][T5]") {
    using namespace orca_cli;
    auto stl = (orca_cli_test::stl_dir() / "000_01_test_cube.stl");
    if (!boost::filesystem::exists(stl)) { SUCCEED("Skipped: no cube fixture"); return; }

    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = stl.string();
    p.object_name = "cube_t5";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE(s.model->objects.back()->name == "cube_t5");
}

// -- T4: split_object_to_parts -------------------------------------------------

TEST_CASE("split_object_to_parts produces 2 volumes from two_cubes.stl",
          "[orca-cli][split][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "two_cubes_a";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE(find_object(s, "two_cubes_a") != nullptr);

    REQUIRE_NOTHROW(split_object_to_parts(s, "two_cubes_a"));
    auto* obj = find_object(s, "two_cubes_a");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    // libslic3r convention: post-split volumes named {original}_1, _2, ...
    REQUIRE(obj->volumes[0]->name == std::string("two_cubes_a_1"));
    REQUIRE(obj->volumes[1]->name == std::string("two_cubes_a_2"));
}

TEST_CASE("split_object_to_parts on single-component mesh throws invalid_argument",
          "[orca-cli][split][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_STL_DIR) / "000_01_test_cube.stl").string();
    p.object_name = "single_cube";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_THROWS_AS(split_object_to_parts(s, "single_cube"), std::invalid_argument);
}

TEST_CASE("split_object_to_parts on already-split object throws invalid_argument",
          "[orca-cli][split][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "two_cubes_b";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "two_cubes_b"));
    // Second call must fail because the object now has 2 volumes.
    REQUIRE_THROWS_AS(split_object_to_parts(s, "two_cubes_b"), std::invalid_argument);
}

TEST_CASE("split_object_to_parts on unknown object throws out_of_range",
          "[orca-cli][split][unit]") {
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    REQUIRE_THROWS_AS(split_object_to_parts(s, "__nope__"), std::out_of_range);
}

TEST_CASE("stamp_source_if_missing leaves already-set source unchanged "
          "(propagation case)",
          "[orca-cli][split][unit][source-attribution]") {
    using namespace orca_cli;
    using namespace Slic3r;

    ModelVolume::Source fallback;
    fallback.input_file = "/tmp/should-not-be-used.stl";
    fallback.object_idx = 99;
    fallback.volume_idx = 99;

    // Synthesize a ModelVolume with source already populated. Direct
    // construction via Model+ModelObject because ModelVolume has no public
    // default ctor.
    Model m;
    ModelObject* obj = m.add_object();
    TriangleMesh empty_mesh;
    ModelVolume* vol = obj->add_volume(empty_mesh);
    vol->source.input_file = "/real/path.stl";
    vol->source.object_idx = 7;
    vol->source.volume_idx = 3;

    stamp_source_if_missing(*vol, fallback);

    REQUIRE(vol->source.input_file == std::string("/real/path.stl"));
    REQUIRE(vol->source.object_idx == 7);
    REQUIRE(vol->source.volume_idx == 3);
}

TEST_CASE("stamp_source_if_missing stamps from fallback when input_file is empty "
          "(regression simulation case - simulates future libslic3r breakage)",
          "[orca-cli][split][unit][source-attribution]") {
    using namespace orca_cli;
    using namespace Slic3r;

    ModelVolume::Source fallback;
    fallback.input_file = "/real/path.stl";
    fallback.object_idx = 7;
    fallback.volume_idx = 3;

    Model m;
    ModelObject* obj = m.add_object();
    TriangleMesh empty_mesh;
    ModelVolume* vol = obj->add_volume(empty_mesh);
    // Manually clear source to simulate a hypothetical future libslic3r
    // change where ModelVolume::split does NOT propagate source to children.
    vol->source = ModelVolume::Source();
    REQUIRE(vol->source.input_file.empty());

    stamp_source_if_missing(*vol, fallback);

    REQUIRE(vol->source.input_file == std::string("/real/path.stl"));
    REQUIRE(vol->source.object_idx == 7);
    REQUIRE(vol->source.volume_idx == 3);
}

TEST_CASE("split_object_to_parts preserves source attribution on every "
          "new volume (Bug C lock-in)",
          "[orca-cli][split][unit][source-attribution]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "two_cubes_src";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "two_cubes_src"));

    auto* obj = find_object(s, "two_cubes_src");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    for (size_t i = 0; i < obj->volumes.size(); ++i) {
        DYNAMIC_SECTION("volume " << i << " source attribution") {
            const auto& src = obj->volumes[i]->source;
            INFO("input_file=" << src.input_file
                 << " object_idx=" << src.object_idx
                 << " volume_idx=" << src.volume_idx);
            REQUIRE_FALSE(src.input_file.empty());
            REQUIRE(src.input_file == p.stl_path);
        }
    }
}

// -- T6: set_object_filament with optional part_name --------------------------

TEST_CASE("set_object_filament without part_name still hits object-level config "
          "(regression pin for existing P5 behaviour)",
          "[orca-cli][split][unit]") {
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    // Take whichever object the reference 3mf provides first.
    REQUIRE_FALSE(s.model->objects.empty());
    const std::string name = s.model->objects.front()->name;
    REQUIRE_NOTHROW(set_object_filament(s, name, 2));
    ModelObject& obj = find_object_or_throw(s, name);
    auto* opt = obj.config.get().opt<ConfigOptionInt>("extruder");
    REQUIRE(opt != nullptr);
    REQUIRE(opt->value == 2);
}

TEST_CASE("set_object_filament with part_name writes to the named volume's config",
          "[orca-cli][split][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "two_cubes_f";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "two_cubes_f"));

    REQUIRE_NOTHROW(set_object_filament(s, "two_cubes_f", 2,
                                        std::optional<std::string>("two_cubes_f_1")));
    auto* obj = find_object(s, "two_cubes_f");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);

    auto* vol0_opt = obj->volumes[0]->config.get().opt<ConfigOptionInt>("extruder");
    REQUIRE(vol0_opt != nullptr);
    REQUIRE(vol0_opt->value == 2);

    // Volume 1 must NOT have its config set to 2.
    auto* vol1_opt = obj->volumes[1]->config.get().opt<ConfigOptionInt>("extruder");
    if (vol1_opt != nullptr) {
        REQUIRE(vol1_opt->value != 2);
    }
}

TEST_CASE("set_object_filament with unknown part_name throws out_of_range",
          "[orca-cli][split][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    ProjectState s = load_project(ORCA_CLI_REF_3MF);
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "two_cubes_g";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "two_cubes_g"));

    REQUIRE_THROWS_AS(
        set_object_filament(s, "two_cubes_g", 2,
                            std::optional<std::string>("__nope__")),
        std::out_of_range);
}

TEST_CASE("merge_object_parts on Layer A two-source merge produces 1 volume "
          "from 2",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_happy";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_happy"));
    auto* obj = find_object(s, "merge_happy");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_happy",
        {"merge_happy_1", "merge_happy_2"}, "merge_main", std::nullopt));

    auto* obj2 = find_object(s, "merge_happy");
    REQUIRE(obj2 != nullptr);
    REQUIRE(obj2->volumes.size() == 1);
    REQUIRE(obj2->volumes[0]->name == std::string("merge_main"));
}

TEST_CASE("merge_object_parts on unknown object throws out_of_range "
          "(case 6)",
          "[orca-cli][merge][unit]") {
    using namespace orca_cli;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    REQUIRE_THROWS_AS(
        merge_object_parts(s, "__missing__", {"a", "b"}, "m", std::nullopt),
        std::out_of_range);
}

TEST_CASE("merge_object_parts on unknown source name throws out_of_range "
          "(case 5)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_unknown_src";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_unknown_src"));

    REQUIRE_THROWS_AS(
        merge_object_parts(s, "merge_unknown_src",
            {"merge_unknown_src_1", "__nope__"}, "merged", std::nullopt),
        std::out_of_range);
}

TEST_CASE("merge_object_parts refuses --into collision with non-source "
          "(case 8 -> DuplicateNameError)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_collide";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_collide"));

    // Add a third volume by hand-construction so we have a non-source
    // volume to collide with. Use an empty mesh to keep the test fast;
    // the collision check runs BEFORE empty-mesh validation in the
    // precedence chain (cases 12/13 come after case 8).
    ModelObject* obj = find_object(s, "merge_collide");
    REQUIRE(obj != nullptr);
    TriangleMesh dummy;
    ModelVolume* extra = obj->add_volume(dummy);
    extra->name = "merge_collide_extra";

    REQUIRE_THROWS_AS(
        merge_object_parts(s, "merge_collide",
            {"merge_collide_1", "merge_collide_2"},
            "merge_collide_extra",  // collides with non-source name
            std::nullopt),
        DuplicateNameError);
}

TEST_CASE("merge_object_parts allows --into matching a source name "
          "(case 9 -- source consumed, name reused)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_reuse";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_reuse"));

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_reuse",
        {"merge_reuse_1", "merge_reuse_2"},
        "merge_reuse_1",  // matches a source name -- allowed
        std::nullopt));

    auto* obj = find_object(s, "merge_reuse");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(obj->volumes[0]->name == std::string("merge_reuse_1"));
}

TEST_CASE("merge_object_parts refuses non-MODEL_PART source "
          "(case 11 -> invalid_state)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_modifier";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_modifier"));

    // Convert the second volume into a modifier so it's no longer
    // MODEL_PART. The merge should refuse with invalid_argument
    // (maps to ExitCode::invalid_state at the CLI layer).
    ModelObject* obj = find_object(s, "merge_modifier");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    obj->volumes[1]->set_type(ModelVolumeType::PARAMETER_MODIFIER);

    REQUIRE_THROWS_AS(
        merge_object_parts(s, "merge_modifier",
            {"merge_modifier_1", "merge_modifier_2"},
            "merge_modifier_main", std::nullopt),
        std::invalid_argument);
}

TEST_CASE("merge_object_parts refuses when <2 non-empty sources remain "
          "(case 13 -> invalid_state)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_empty";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_empty"));

    // Replace one of the two volumes' meshes with an empty mesh,
    // simulating a stale / broken volume. After dropping empties only
    // 1 non-empty source remains -> case 13 refusal.
    ModelObject* obj = find_object(s, "merge_empty");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    obj->volumes[1]->reset_mesh();

    REQUIRE_THROWS_AS(
        merge_object_parts(s, "merge_empty",
            {"merge_empty_1", "merge_empty_2"},
            "merge_empty_main", std::nullopt),
        std::invalid_argument);
}

namespace {
// Helper for the filament-agreement tests. Returns the effective
// extruder of a volume: per-volume override if set, else object-level
// override if set, else 1. Mirrors object_volume_info's logic.
int effective_extruder(const Slic3r::ModelObject& obj,
                       const Slic3r::ModelVolume& v) {
    using namespace Slic3r;
    if (auto* ve = v.config.get().opt<ConfigOptionInt>("extruder"))
        return ve->value;
    if (auto* oe = obj.config.get().opt<ConfigOptionInt>("extruder"))
        return oe->value;
    return 1;
}
} // namespace

TEST_CASE("merge_object_parts inherits filament when sources agree "
          "(test #9, no --filament needed)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_fil_agree";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_fil_agree"));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_agree", 2,
        std::optional<std::string>("merge_fil_agree_1")));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_agree", 2,
        std::optional<std::string>("merge_fil_agree_2")));

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_fil_agree",
        {"merge_fil_agree_1", "merge_fil_agree_2"},
        "merge_fil_agree_main", std::nullopt));

    auto* obj = find_object(s, "merge_fil_agree");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(effective_extruder(*obj, *obj->volumes[0]) == 2);
}

TEST_CASE("merge_object_parts refuses filament conflict without override "
          "(case 10 -> invalid_state, test #7)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_fil_conflict";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_fil_conflict"));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_conflict", 1,
        std::optional<std::string>("merge_fil_conflict_1")));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_conflict", 2,
        std::optional<std::string>("merge_fil_conflict_2")));

    REQUIRE_THROWS_AS(
        merge_object_parts(s, "merge_fil_conflict",
            {"merge_fil_conflict_1", "merge_fil_conflict_2"},
            "merge_fil_conflict_main", std::nullopt),
        std::invalid_argument);
}

TEST_CASE("merge_object_parts applies filament override on conflict "
          "(test #8)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_fil_over";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_fil_over"));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_over", 1,
        std::optional<std::string>("merge_fil_over_1")));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_over", 2,
        std::optional<std::string>("merge_fil_over_2")));

    REQUIRE_NOTHROW(merge_object_parts(s, "merge_fil_over",
        {"merge_fil_over_1", "merge_fil_over_2"},
        "merge_fil_over_main", std::optional<int>(2)));

    auto* obj = find_object(s, "merge_fil_over");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(effective_extruder(*obj, *obj->volumes[0]) == 2);
}

TEST_CASE("merge_object_parts honours explicit --filament override on "
          "agreeing sources (test #15)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_fil_explicit";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_fil_explicit"));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_explicit", 1,
        std::optional<std::string>("merge_fil_explicit_1")));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_explicit", 1,
        std::optional<std::string>("merge_fil_explicit_2")));

    // Sources agree on extruder=1; override to 2 must win.
    REQUIRE_NOTHROW(merge_object_parts(s, "merge_fil_explicit",
        {"merge_fil_explicit_1", "merge_fil_explicit_2"},
        "merge_fil_explicit_main", std::optional<int>(2)));

    auto* obj = find_object(s, "merge_fil_explicit");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 1);
    REQUIRE(effective_extruder(*obj, *obj->volumes[0]) == 2);
}

TEST_CASE("merge_object_parts excludes empty sources from filament "
          "agreement check (test #16)",
          "[orca-cli][merge][unit]") {
    namespace fs = boost::filesystem;
    using namespace orca_cli;
    using namespace Slic3r;
    if (orca_cli_test::ref_3mf().empty()) { SUCCEED("Skipped: no reference 3mf"); return; }
    auto s = load_project(orca_cli_test::ref_3mf().string());
    AddObjectParams p;
    p.plate_name  = s.plates.front()->plate_name;
    p.stl_path    = (fs::path(ORCA_CLI_FIXTURES_DIR) / "two_cubes.stl").string();
    p.object_name = "merge_fil_skipempty";
    p.count       = 1;
    REQUIRE_NOTHROW(add_object(s, p));
    REQUIRE_NOTHROW(split_object_to_parts(s, "merge_fil_skipempty"));

    // Add a third volume (initially with non-empty mesh from add_volume
    // -- the only way to construct a ModelVolume), then reset its mesh
    // and set its extruder to a deliberately-conflicting value. The
    // empty source must NOT contribute to the agreement check.
    ModelObject* obj = find_object(s, "merge_fil_skipempty");
    REQUIRE(obj != nullptr);
    REQUIRE(obj->volumes.size() == 2);
    TriangleMesh dummy(obj->volumes[0]->mesh()); // copy a non-empty mesh
    ModelVolume* extra = obj->add_volume(dummy);
    extra->name = "merge_fil_skipempty_empty";
    extra->config.set("extruder", 2);
    extra->reset_mesh();   // now empty

    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_skipempty", 1,
        std::optional<std::string>("merge_fil_skipempty_1")));
    REQUIRE_NOTHROW(set_object_filament(s, "merge_fil_skipempty", 1,
        std::optional<std::string>("merge_fil_skipempty_2")));

    // Three "sources" -- the empty one has ext=2 but should be excluded.
    // The two non-empty sources agree on ext=1; no --filament needed.
    REQUIRE_NOTHROW(merge_object_parts(s, "merge_fil_skipempty",
        {"merge_fil_skipempty_1", "merge_fil_skipempty_2",
         "merge_fil_skipempty_empty"},
        "merge_fil_skipempty_main", std::nullopt));

    auto* obj2 = find_object(s, "merge_fil_skipempty");
    REQUIRE(obj2 != nullptr);
    REQUIRE(obj2->volumes.size() == 1);
    REQUIRE(effective_extruder(*obj2, *obj2->volumes[0]) == 1);
}
