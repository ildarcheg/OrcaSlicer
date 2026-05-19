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

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>

namespace orca_cli::commands {

namespace fs = boost::filesystem;

namespace {

int check_input_exists(const GlobalOpts& g, const std::string& path)
{
    if (!fs::exists(path)) {
        print_err(g, ExitCode::file_not_found, "input not found: " + path);
        return int(ExitCode::file_not_found);
    }
    return int(ExitCode::ok);
}

int do_object_add(const GlobalOpts& g,
                  const std::string& file,
                  const std::string& plate,
                  const std::string& stl,
                  int                count,
                  const std::string& name)
{
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok))
        return rc;
    if (!fs::exists(stl)) {
        print_err(g, ExitCode::file_not_found, "stl not found: " + stl);
        return int(ExitCode::file_not_found);
    }

    const std::string out = resolve_save_target(g, file);
    try {
        auto state = load_project(file);
        AddObjectParams p;
        p.plate_name  = plate;
        p.stl_path    = stl;
        p.object_name = name;
        p.count       = count;
        add_object(state, p);
        save_project(state, out);
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
    static int         add_count = 1;
    static std::string rm_file,  rm_name;
    static std::string ls_file;

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
    add->add_option("--output", g.output,
                    "write result to this path instead of overwriting input");
    add->callback([&g]() {
        std::exit(do_object_add(g, add_file, add_plate, add_stl,
                                add_count, add_name));
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
