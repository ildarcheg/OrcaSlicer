// inspect.cpp -- `orca-cli inspect <file>` read-only diagnostic dump.
//
// Reports plate count, per-plate object names, filament slot count,
// project-level changed config keys (vs libslic3r defaults), and the
// explicitly-set per-object config keys for every ModelObject in the
// loaded project. Read-only -- rejects --output with usage_error the
// same way the other `list`-shaped subcommands do.
#include "inspect.hpp"

#include "../cli11/CLI11.hpp"
#include "../io.hpp"
#include "../output.hpp"
#include "../project_ops.hpp"

#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/Format/bbs_3mf.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace orca_cli::commands {

namespace {

int filament_slot_count(const ProjectState& state)
{
    if (!state.project_config) return 0;
    const auto* fsid = state.project_config->option<Slic3r::ConfigOptionStrings>(
        "filament_settings_id");
    return fsid ? int(fsid->values.size()) : 0;
}

int do_inspect(const GlobalOpts& g, const std::string& file)
{
    // --output is reserved for mutating subcommands; inspect is read-only.
    // Rejecting explicitly (same as plate/object/config list) so the user
    // sees `usage_error` instead of a silently ignored flag.
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error,
                  "inspect does not accept --output");
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

    const int plate_count = int(state.plates.size());
    const int filament_count = filament_slot_count(state);

    // Project-level keys that differ from libslic3r defaults. P6's
    // changed_project_keys takes the defaults via new_from_defaults_keys
    // (spec G6 -- avoids default_value->serialize() on coEnums).
    std::vector<std::string> project_changed;
    try {
        project_changed = changed_project_keys(state);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }

    if (g.json) {
        // Emit the structured report inside print_ok's `data` object.
        // print_ok wraps `{...}` around the string we hand it, so we
        // contribute the inner key:value pairs only.
        std::ostringstream js;
        js << "\"plate_count\":"    << plate_count
           << ",\"filament_count\":" << filament_count;

        js << ",\"plates\":[";
        for (size_t i = 0; i < state.plates.size(); ++i) {
            if (i) js << ",";
            const auto& pd = state.plates[i];
            js << "{\"index\":" << (i + 1)
               << ",\"name\":\"" << escape_json(pd->plate_name) << "\""
               << ",\"objects\":[";
            bool first = true;
            for (const auto& kv : pd->objects_and_instances) {
                const int oi = kv.first;
                if (oi < 0 || oi >= int(state.model->objects.size()))
                    continue; // defensive: stale obj_idx
                if (!first) js << ",";
                first = false;
                js << "\"" << escape_json(state.model->objects[oi]->name) << "\"";
            }
            js << "]}";
        }
        js << "]";

        js << ",\"project_changed\":[";
        for (size_t i = 0; i < project_changed.size(); ++i) {
            if (i) js << ",";
            js << "\"" << escape_json(project_changed[i]) << "\"";
        }
        js << "]";

        js << ",\"objects\":[";
        for (size_t i = 0; i < state.model->objects.size(); ++i) {
            if (i) js << ",";
            const auto* obj = state.model->objects[i];
            const auto keys = obj->config.keys();
            js << "{\"name\":\"" << escape_json(obj->name) << "\""
               << ",\"config_keys\":[";
            for (size_t k = 0; k < keys.size(); ++k) {
                if (k) js << ",";
                js << "\"" << escape_json(keys[k]) << "\"";
            }
            js << "]}";
        }
        js << "]";

        print_ok(g, "inspected " + file, js.str());
    } else {
        // Human mode -- one line per fact, indented to make the
        // per-plate / per-object grouping visible at a glance.
        std::fputs(("plates: " + std::to_string(plate_count) + "\n").c_str(),
                   stdout);
        std::fputs(("filament slots: " + std::to_string(filament_count) + "\n").c_str(),
                   stdout);
        for (size_t i = 0; i < state.plates.size(); ++i) {
            const auto& pd = state.plates[i];
            std::fputs(("  plate " + std::to_string(i + 1) + ": "
                        + pd->plate_name + "\n").c_str(),
                       stdout);
            for (const auto& kv : pd->objects_and_instances) {
                const int oi = kv.first;
                if (oi < 0 || oi >= int(state.model->objects.size()))
                    continue;
                std::fputs(("    object: "
                            + state.model->objects[oi]->name + "\n").c_str(),
                           stdout);
            }
        }
        std::fputs(("project changed keys: "
                    + std::to_string(project_changed.size()) + "\n").c_str(),
                   stdout);
        for (const auto& k : project_changed)
            std::fputs(("  " + k + "\n").c_str(), stdout);
        for (const auto* obj : state.model->objects) {
            const auto keys = obj->config.keys();
            std::fputs(("object: " + obj->name
                        + " (" + std::to_string(keys.size())
                        + " per-object keys)\n").c_str(),
                       stdout);
            for (const auto& k : keys)
                std::fputs(("  " + k + "\n").c_str(), stdout);
        }
        std::fflush(stdout);
    }
    return int(ExitCode::ok);
}

} // namespace

void register_inspect_subcmd(CLI::App& app, GlobalOpts& g)
{
    auto* ins = app.add_subcommand("inspect",
        "diagnostic dump of a .3mf project (read-only)");

    // CLI11 binds options to long-lived storage by reference. Static keeps
    // the value alive across the parse / callback boundary -- matches the
    // pattern used by every other subcommand in this directory.
    static std::string ins_file;
    ins->add_option("file", ins_file, "input .3mf path")->required();
    // Register --output here so we can reject it explicitly with
    // usage_error rather than CLI11's generic "unknown option" exit.
    // Same rationale as plate/object/config list.
    ins->add_option("--output", g.output,
                    "(rejected on inspect; read-only command)");
    ins->callback([&g]() {
        std::exit(do_inspect(g, ins_file));
    });
}

} // namespace orca_cli::commands
