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

#include <nlohmann/json.hpp>

#include <libslic3r/Model.hpp>
#include <libslic3r/Format/bbs_3mf.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace orca_cli::commands {

namespace {

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
    const int filament_count = orca_cli::filament_slot_count(*state.project_config);

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
        nlohmann::json data;
        data["plate_count"]    = plate_count;
        data["filament_count"] = filament_count;

        auto& plates_arr = data["plates"] = nlohmann::json::array();
        for (size_t i = 0; i < state.plates.size(); ++i) {
            const auto& pd = state.plates[i];
            nlohmann::json plate_obj;
            plate_obj["index"] = int(i + 1);
            plate_obj["name"]  = pd->plate_name;
            auto& objs_arr = plate_obj["objects"] = nlohmann::json::array();
            for (const auto& kv : pd->objects_and_instances) {
                const int oi = kv.first;
                if (oi < 0 || oi >= int(state.model->objects.size()))
                    continue; // defensive: stale obj_idx
                objs_arr.push_back(state.model->objects[oi]->name);
            }
            plates_arr.push_back(std::move(plate_obj));
        }

        auto& changed_arr = data["project_changed"] = nlohmann::json::array();
        for (const auto& k : project_changed)
            changed_arr.push_back(k);

        auto& objs_arr = data["objects"] = nlohmann::json::array();
        for (const auto* obj : state.model->objects) {
            nlohmann::json obj_entry;
            obj_entry["name"] = obj->name;
            auto& cfg_keys = obj_entry["config_keys"] = nlohmann::json::array();
            for (const auto& k : obj->config.keys())
                cfg_keys.push_back(k);
            objs_arr.push_back(std::move(obj_entry));
        }

        print_ok(g, "inspected " + file, data);
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
