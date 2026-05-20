// object.cpp -- `orca-cli object {add,remove,list}` subcommand wiring.
// Mirrors the shape of commands/plate.cpp: per-subcommand option statics,
// callbacks dispatching into project_ops mutations, exit-code mapping
// via the standard exception -> ExitCode pattern. See the module-level
// comment in src/cli/commands/plate.cpp for the rationale.
#include "object.hpp"

#include "../cli11/CLI11.hpp"
#include "../invariants.hpp"
#include "../io.hpp"
#include "../output.hpp"
#include "../project_ops.hpp"

#include <boost/filesystem.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace orca_cli::commands {

namespace fs = boost::filesystem;

namespace {

// Parse a vector flag value:
//   "x,y"     -> (x, y, 0)
//   "x,y,z"   -> (x, y, z)
//   "s"       -> (s, s, s)  -- only when allow_scalar is true
// Returns std::nullopt on parse failure or if any component is non-finite.
// Split on ',' strictly. Each comma-delimited token must be non-empty
// (after whitespace trim) and parse to a finite double. Consecutive
// commas / leading or trailing commas are rejected -- e.g. "60,,60"
// is NOT silently collapsed to "60,60".
std::optional<Slic3r::Vec3d> parse_vec3(const std::string& s, bool allow_scalar)
{
    if (s.empty()) return std::nullopt;
    std::vector<double> nums;
    std::string token;
    auto trim = [](std::string& t) {
        size_t a = t.find_first_not_of(" \t");
        size_t b = t.find_last_not_of(" \t");
        if (a == std::string::npos) t.clear();
        else t = t.substr(a, b - a + 1);
    };
    auto flush = [&]() -> bool {
        trim(token);
        if (token.empty()) return false;
        try {
            size_t consumed = 0;
            double v = std::stod(token, &consumed);
            if (consumed != token.size())   return false;
            if (!std::isfinite(v))          return false;
            nums.push_back(v);
        } catch (...) { return false; }
        token.clear();
        return true;
    };
    for (char ch : s) {
        if (ch == ',') {
            if (!flush()) return std::nullopt;
        } else {
            token.push_back(ch);
        }
    }
    if (!flush()) return std::nullopt;

    if (allow_scalar && nums.size() == 1) return Slic3r::Vec3d(nums[0], nums[0], nums[0]);
    if (nums.size() == 2) return Slic3r::Vec3d(nums[0], nums[1], 0.0);
    if (nums.size() == 3) return Slic3r::Vec3d(nums[0], nums[1], nums[2]);
    return std::nullopt;
}

int do_object_add(const GlobalOpts& g,
                  const std::string& file,
                  const std::string& plate,
                  const std::string& stl,
                  int                count,
                  const std::string& name,
                  const std::string& translate_str,
                  const std::string& rotate_str,
                  const std::string& scale_str,
                  // P5: --filament. 0 is the unset sentinel (CLI11 leaves
                  // the bound int at its default when the flag is absent).
                  // Any negative or positive value is forwarded to
                  // set_object_filament for validation.
                  int                filament_slot)
{
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok))
        return rc;
    if (!fs::exists(stl)) {
        print_err(g, ExitCode::file_not_found, "stl not found: " + stl);
        return int(ExitCode::file_not_found);
    }

    // Parse transform flags up-front so a bad value is reported as a
    // usage_error before we touch the archive. CLI11 leaves the strings
    // empty when the flag wasn't supplied.
    std::optional<Slic3r::Vec3d> translate, rotate, scale;
    if (!translate_str.empty()) {
        translate = parse_vec3(translate_str, /*allow_scalar=*/false);
        if (!translate) {
            print_err(g, ExitCode::usage_error,
                      "invalid --translate value '" + translate_str +
                      "' (expected x,y or x,y,z)");
            return int(ExitCode::usage_error);
        }
    }
    if (!rotate_str.empty()) {
        rotate = parse_vec3(rotate_str, /*allow_scalar=*/false);
        // --rotate requires all three components; reject the 2-component
        // form parse_vec3 would otherwise accept.
        bool ok = rotate.has_value();
        if (ok) {
            // Re-parse to check the original had 3 comma-separated parts.
            int commas = 0;
            for (char c : rotate_str) if (c == ',') ++commas;
            if (commas != 2) ok = false;
        }
        if (!ok) {
            print_err(g, ExitCode::usage_error,
                      "invalid --rotate value '" + rotate_str +
                      "' (expected ax,ay,az in radians)");
            return int(ExitCode::usage_error);
        }
    }
    if (!scale_str.empty()) {
        // --scale accepts either a uniform scalar (0 commas) or all three
        // components (2 commas). Reject the 2-component form explicitly:
        // parse_vec3(..., allow_scalar=true) would otherwise accept "1,2"
        // as (1,2,0), and set_scaling_factor((1,2,0)) silently collapses
        // the object to a Z=0 sheet -- the off-bed check only inspects
        // X/Y bounds and doesn't catch it.
        int commas = int(std::count(scale_str.begin(), scale_str.end(), ','));
        if (commas != 0 && commas != 2) {
            print_err(g, ExitCode::usage_error,
                      "invalid --scale value '" + scale_str +
                      "' (expected s or sx,sy,sz)");
            return int(ExitCode::usage_error);
        }
        scale = parse_vec3(scale_str, /*allow_scalar=*/true);
        if (!scale) {
            print_err(g, ExitCode::usage_error,
                      "invalid --scale value '" + scale_str +
                      "' (expected s or sx,sy,sz)");
            return int(ExitCode::usage_error);
        }
    }

    const std::string out = resolve_save_target(g, file);
    try {
        auto state = load_project(file);
        AddObjectParams p;
        p.plate_name  = plate;
        p.stl_path    = stl;
        p.object_name = name;
        p.count       = count;
        p.translate   = translate;
        p.rotate      = rotate;
        p.scale       = scale;
        // CLI11 stores filament_slot as a plain int with default 0; we
        // treat 0 as "unset" because libslic3r's extruder index is 1-based
        // (the GUI's filament panel labels start at 1, not 0). Anything
        // else (negative or positive) is forwarded so the validation in
        // set_object_filament reports the same error the user would see
        // from `object set-filament`.
        if (filament_slot != 0) p.filament_slot = filament_slot;
        add_object(state, p);
        save_project(state, out);
    } catch (const PlacementFailure& e) {
        // Off-bed -- spec § 4.3: exit 9 placement_failure.
        print_err(g, ExitCode::placement_failure, e.what());
        return int(ExitCode::placement_failure);
    } catch (const InvariantViolation& e) {
        print_err(g, ExitCode::invariant_violation, e.what());
        return int(ExitCode::invariant_violation);
    } catch (const std::out_of_range& e) {
        // add_object throws this when the plate name is unknown.
        print_err(g, ExitCode::unknown_reference, e.what());
        return int(ExitCode::unknown_reference);
    } catch (const std::invalid_argument& e) {
        // Reserved for future duplicate-object-name guards. Map to the
        // CLI's standard duplicate_name code for symmetry with `plate`.
        print_err(g, ExitCode::duplicate_name, e.what());
        return int(ExitCode::duplicate_name);
    } catch (const std::exception& e) {
        // STL load failure / save failure / etc.
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }

    print_ok(g, "added object from '" + stl + "' to plate '" + plate + "'");
    return int(ExitCode::ok);
}

int do_object_set_filament(const GlobalOpts& g,
                           const std::string& file,
                           const std::string& name,
                           int                slot)
{
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok))
        return rc;

    const std::string out = resolve_save_target(g, file);
    try {
        auto state = load_project(file);
        set_object_filament(state, name, slot);
        save_project(state, out);
    } catch (const InvariantViolation& e) {
        print_err(g, ExitCode::invariant_violation, e.what());
        return int(ExitCode::invariant_violation);
    } catch (const std::out_of_range& e) {
        // Both "object not found" and "slot out of range" surface as
        // std::out_of_range from set_object_filament; both map to
        // unknown_reference per the P5 plan.
        print_err(g, ExitCode::unknown_reference, e.what());
        return int(ExitCode::unknown_reference);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    print_ok(g, "set filament " + std::to_string(slot) +
                " on object '" + name + "'");
    return int(ExitCode::ok);
}

int do_object_remove(const GlobalOpts& g,
                     const std::string& file,
                     const std::string& name)
{
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok))
        return rc;

    const std::string out = resolve_save_target(g, file);
    try {
        auto state = load_project(file);
        remove_object(state, name);
        save_project(state, out);
    } catch (const InvariantViolation& e) {
        print_err(g, ExitCode::invariant_violation, e.what());
        return int(ExitCode::invariant_violation);
    } catch (const std::out_of_range& e) {
        print_err(g, ExitCode::unknown_reference, e.what());
        return int(ExitCode::unknown_reference);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }

    print_ok(g, "removed object '" + name + "'");
    return int(ExitCode::ok);
}

int do_object_list(const GlobalOpts& g, const std::string& file)
{
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error,
                  "object list does not accept --output");
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok))
        return rc;

    ProjectState state;
    try {
        state = load_project(file);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }

    // Build (object_name, plate_name) tuples by walking
    // PlateData::objects_and_instances. An object on no plate (rare but
    // possible after `object remove` edge cases or weird input 3mfs) is
    // still emitted with an empty plate name so the user can see it.
    struct Row { std::string object; std::string plate; };
    std::vector<Row> rows;
    std::vector<bool> on_plate(state.model->objects.size(), false);
    for (const auto& pd : state.plates) {
        for (const auto& kv : pd->objects_and_instances) {
            int oi = kv.first;
            if (oi < 0 || oi >= int(state.model->objects.size())) continue;
            if (on_plate[oi]) continue; // one row per (object, first-plate)
            on_plate[oi] = true;
            rows.push_back({state.model->objects[oi]->name, pd->plate_name});
        }
    }
    // Objects that aren't on any plate -- still surface them.
    for (size_t i = 0; i < state.model->objects.size(); ++i) {
        if (on_plate[i]) continue;
        rows.push_back({state.model->objects[i]->name, std::string{}});
    }

    if (g.json) {
        std::string objs_json = "\"objects\":[";
        bool first = true;
        for (const auto& r : rows) {
            if (!first) objs_json += ",";
            first = false;
            objs_json += "{\"name\":\""  + escape_json(r.object) + "\""
                      +  ",\"plate\":\"" + escape_json(r.plate)  + "\"}";
        }
        objs_json += "]";
        print_ok(g,
                 "listed " + std::to_string(rows.size()) + " objects",
                 objs_json);
    } else {
        for (const auto& r : rows) {
            std::string line = "object: " + r.object
                             + " on plate " + r.plate + "\n";
            std::fputs(line.c_str(), stdout);
        }
        std::fflush(stdout);
    }
    return int(ExitCode::ok);
}

} // namespace

void register_object_subcmd(CLI::App& app, GlobalOpts& g)
{
    auto* obj = app.add_subcommand("object", "object-level operations");

    // Per-subcommand static storage so each parse starts from a clean
    // slate even when cli_tests reuses the same process across multiple
    // run_cli() invocations via the in-process spawn path. CLI11 binds
    // by reference.
    static std::string add_file, add_plate, add_stl, add_name;
    static std::string add_translate, add_rotate, add_scale;
    static int         add_count = 1;
    // P5: --filament on `object add`. 0 means "unset"; the callback only
    // forwards non-zero values to AddObjectParams::filament_slot.
    static int         add_filament = 0;
    static std::string rm_file,  rm_name;
    static std::string ls_file;
    // P5: state for `object set-filament`.
    static std::string sf_file, sf_name;
    static int         sf_slot = 0;

    // -- object add --------------------------------------------------------
    auto* add = obj->add_subcommand("add", "add an STL to a plate");
    add->add_option("file",     add_file,  "input .3mf path")->required();
    add->add_option("--plate",  add_plate, "target plate name")->required();
    // G9 note: option named `--stl` (not `--file`) to avoid the CLI11 +
    // MSVC /GS interaction that aborts the process when an option named
    // `--file` collides with the positional `file`.
    add->add_option("--stl",    add_stl,   "STL path to load")->required();
    add->add_option("--count",  add_count, "number of instances (default 1)");
    add->add_option("--name",   add_name,  "object name (default: STL basename)");
    // Transform flags (P4). Captured as raw strings so we preserve
    // "unset" vs "explicitly empty" and report parse failures as
    // usage_error in the callback. When any is supplied, --count N
    // stacks all N instances at the same post-transform position
    // (spec § 4.3) instead of grid-placing them.
    add->add_option("--translate", add_translate,
                    "plate-local offset, x,y or x,y,z (mm)");
    add->add_option("--rotate", add_rotate,
                    "Euler XYZ rotation in radians: ax,ay,az");
    add->add_option("--scale", add_scale,
                    "scaling factor: uniform s, or sx,sy,sz");
    // P5: filament slot (1-based) -- forwarded to set_object_filament after
    // instance placement. Out-of-range / unknown-object both throw
    // std::out_of_range and surface as exit 6 (unknown_reference).
    add->add_option("--filament", add_filament,
                    "filament slot to assign (1-based)");
    add->add_option("--output", g.output,
                    "write result to this path instead of overwriting input");
    add->callback([&g]() {
        std::exit(do_object_add(g, add_file, add_plate, add_stl,
                                add_count, add_name,
                                add_translate, add_rotate, add_scale,
                                add_filament));
    });

    // -- object remove -----------------------------------------------------
    auto* rm = obj->add_subcommand("remove", "remove an object by name");
    rm->add_option("file",     rm_file, "input .3mf path")->required();
    rm->add_option("--name",   rm_name, "object name to remove")->required();
    rm->add_option("--output", g.output,
                   "write result to this path instead of overwriting input");
    rm->callback([&g]() {
        std::exit(do_object_remove(g, rm_file, rm_name));
    });

    // -- object set-filament (P5) -----------------------------------------
    // Retroactively assign a filament slot to an existing object. Same
    // validation rules as `object add --filament`: slot must be in
    // [1, filament_settings_id.size()]; out-of-range OR unknown-object
    // both exit 6 (unknown_reference).
    auto* setf = obj->add_subcommand("set-filament",
        "assign a filament slot to an existing object");
    setf->add_option("file",     sf_file, "input .3mf path")->required();
    setf->add_option("--name",   sf_name, "object name to update")->required();
    setf->add_option("--filament", sf_slot,
                     "filament slot (1-based)")->required();
    setf->add_option("--output", g.output,
                     "write result to this path instead of overwriting input");
    setf->callback([&g]() {
        std::exit(do_object_set_filament(g, sf_file, sf_name, sf_slot));
    });

    // -- object list -------------------------------------------------------
    auto* ls = obj->add_subcommand("list", "list objects in a project");
    ls->add_option("file", ls_file, "input .3mf path")->required();
    // See commands/plate.cpp for the rationale on registering --output
    // here just to reject it explicitly with usage_error rather than the
    // CLI11 "unknown option" exit code.
    ls->add_option("--output", g.output,
                   "(rejected on object list; mutating subcommands only)");
    ls->callback([&g]() {
        std::exit(do_object_list(g, ls_file));
    });
}

} // namespace orca_cli::commands
