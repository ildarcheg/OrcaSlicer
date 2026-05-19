#include "plate.hpp"

#include "../cli11/CLI11.hpp"
#include "../io.hpp"
#include "../invariants.hpp"
#include "../output.hpp"
#include "../project_ops.hpp"

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>

namespace orca_cli::commands {

namespace fs = boost::filesystem;

namespace {

// Common preflight: file must exist on disk. Returns ExitCode::file_not_found
// when missing, otherwise ExitCode::ok.
int check_input_exists(const GlobalOpts& g, const std::string& path)
{
    if (!fs::exists(path)) {
        print_err(g, ExitCode::file_not_found, "input not found: " + path);
        return int(ExitCode::file_not_found);
    }
    return int(ExitCode::ok);
}

// Map common mutation exceptions to CLI exit codes. Caller passes a
// std::exception pointer (already caught) and we emit the appropriate
// err line + return the int exit code. invariant_violation is handled
// inline in run_mutation so it can also clean up partial state.
int map_mutation_exception(const GlobalOpts& g, const std::exception& e)
{
    // We get here only on std::exception subclasses we did not catch
    // specifically. The most useful classification we can do is via
    // dynamic_cast: invalid_argument -> duplicate_name; out_of_range ->
    // unknown_reference. Anything else maps to parse_failure (generic).
    if (dynamic_cast<const std::invalid_argument*>(&e)) {
        print_err(g, ExitCode::duplicate_name, e.what());
        return int(ExitCode::duplicate_name);
    }
    if (dynamic_cast<const std::out_of_range*>(&e)) {
        print_err(g, ExitCode::unknown_reference, e.what());
        return int(ExitCode::unknown_reference);
    }
    print_err(g, ExitCode::parse_failure, e.what());
    return int(ExitCode::parse_failure);
}

// Run a mutation on the project loaded from `input`, save to the resolved
// target (input itself or --output), and emit print_ok on success. The
// mutate callback gets a mutable ProjectState&. Any exception raised by
// the callback is mapped to an exit code via map_mutation_exception (or,
// for InvariantViolation, ExitCode::invariant_violation).
template <typename Mutator>
int run_mutation(const GlobalOpts&  g,
                 const std::string& input,
                 const std::string& ok_message,
                 Mutator&&          mutate)
{
    if (int rc = check_input_exists(g, input); rc != int(ExitCode::ok))
        return rc;

    const std::string out = resolve_save_target(g, input);
    try {
        auto state = load_project(input);
        mutate(state);
        save_project(state, out);
    } catch (const InvariantViolation& e) {
        print_err(g, ExitCode::invariant_violation, e.what());
        return int(ExitCode::invariant_violation);
    } catch (const std::exception& e) {
        return map_mutation_exception(g, e);
    }

    print_ok(g, ok_message);
    return int(ExitCode::ok);
}

// Special-case `plate remove` mapping: remove_plate throws
// invalid_argument on the "only plate" guard, which is invalid_state,
// not duplicate_name. We can't disambiguate by exception type alone
// without dedicated exception classes, so this command checks the
// pre-condition before calling remove_plate to keep the mapping crisp.
int do_plate_remove(const GlobalOpts& g, const std::string& input, const std::string& name)
{
    if (int rc = check_input_exists(g, input); rc != int(ExitCode::ok))
        return rc;

    const std::string out = resolve_save_target(g, input);
    try {
        auto state = load_project(input);
        if (state.plates.size() <= 1) {
            print_err(g, ExitCode::invalid_state, "cannot remove the only plate");
            return int(ExitCode::invalid_state);
        }
        remove_plate(state, name);
        save_project(state, out);
    } catch (const InvariantViolation& e) {
        print_err(g, ExitCode::invariant_violation, e.what());
        return int(ExitCode::invariant_violation);
    } catch (const std::out_of_range& e) {
        print_err(g, ExitCode::unknown_reference, e.what());
        return int(ExitCode::unknown_reference);
    } catch (const std::exception& e) {
        // Anything else (load/save failures) -> generic.
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }

    print_ok(g, "removed plate '" + name + "'");
    return int(ExitCode::ok);
}

int do_plate_list(const GlobalOpts& g, const std::string& input)
{
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error, "plate list does not accept --output");
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, input); rc != int(ExitCode::ok))
        return rc;

    ProjectState state;
    try {
        state = load_project(input);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }

    if (g.json) {
        // Build a JSON `plates` array inside the data object that print_ok
        // wraps. print_ok's data_json parameter is the inside of the data
        // object, so we emit `"plates":[{...}, ...]`.
        std::string plates_json = "\"plates\":[";
        bool first = true;
        for (size_t i = 0; i < state.plates.size(); ++i) {
            if (!first) plates_json += ",";
            first = false;
            plates_json += "{\"index\":" + std::to_string(i + 1)
                        + ",\"name\":\""  + escape_json(state.plates[i]->plate_name) + "\""
                        + ",\"object_count\":"
                        + std::to_string(state.plates[i]->objects_and_instances.size())
                        + "}";
        }
        plates_json += "]";
        print_ok(g,
                 "listed " + std::to_string(state.plates.size()) + " plates",
                 plates_json);
    } else {
        for (size_t i = 0; i < state.plates.size(); ++i) {
            // 1-based on display to match on-disk plate_N.png naming.
            const auto& p = state.plates[i];
            std::string line = "plate " + std::to_string(i + 1)
                             + ": " + p->plate_name
                             + " (" + std::to_string(p->objects_and_instances.size())
                             + " objects)\n";
            std::fputs(line.c_str(), stdout);
        }
        std::fflush(stdout);
    }
    return int(ExitCode::ok);
}

} // namespace

void register_plate_subcmd(CLI::App& app, GlobalOpts& g)
{
    auto* plate = app.add_subcommand("plate", "plate-level operations");

    // CLI11 binds options to long-lived storage by reference. statics keep
    // these alive across the parse / callback boundary without forcing the
    // caller to manage their lifetime. Each subcommand uses its own
    // dedicated statics so a stale --name from a previous parse cannot
    // leak across invocations of the same process (the test harness
    // invokes the binary as a child process so this is mostly belt-and-
    // braces, but cli_tests links the same TU and is process-internal).
    static std::string add_file, add_name;
    static std::string rm_file,  rm_name;
    static std::string mv_file,  mv_from, mv_to;
    static std::string ls_file;

    // -- plate add ---------------------------------------------------------
    auto* add = plate->add_subcommand("add", "add a new plate to a project");
    add->add_option("file", add_file, "input .3mf path")->required();
    add->add_option("--name", add_name, "name for the new plate")->required();
    add->add_option("--output", g.output, "write result to this path instead of overwriting input");
    add->callback([&g]() {
        std::exit(run_mutation(g, add_file,
                               "added plate '" + add_name + "'",
                               [](ProjectState& s) { add_plate(s, add_name); }));
    });

    // -- plate remove ------------------------------------------------------
    auto* rm = plate->add_subcommand("remove", "remove a plate by name");
    rm->add_option("file", rm_file, "input .3mf path")->required();
    rm->add_option("--name", rm_name, "name of the plate to remove")->required();
    rm->add_option("--output", g.output, "write result to this path instead of overwriting input");
    rm->callback([&g]() {
        std::exit(do_plate_remove(g, rm_file, rm_name));
    });

    // -- plate rename ------------------------------------------------------
    auto* mv = plate->add_subcommand("rename", "rename a plate");
    mv->add_option("file", mv_file, "input .3mf path")->required();
    mv->add_option("--from", mv_from, "current plate name")->required();
    mv->add_option("--to",   mv_to,   "new plate name")->required();
    mv->add_option("--output", g.output, "write result to this path instead of overwriting input");
    mv->callback([&g]() {
        std::exit(run_mutation(g, mv_file,
                               "renamed '" + mv_from + "' -> '" + mv_to + "'",
                               [](ProjectState& s) { rename_plate(s, mv_from, mv_to); }));
    });

    // -- plate list --------------------------------------------------------
    auto* ls = plate->add_subcommand("list", "list plates in a project");
    ls->add_option("file", ls_file, "input .3mf path")->required();
    // Deliberately also register --output here so we can reject it with a
    // crisp usage_error rather than letting CLI11 reject it as unknown
    // (which uses its own exit code that's harder for tests to pin to a
    // single value).
    ls->add_option("--output", g.output, "(rejected on plate list; mutating subcommands only)");
    ls->callback([&g]() {
        std::exit(do_plate_list(g, ls_file));
    });
}

} // namespace orca_cli::commands
