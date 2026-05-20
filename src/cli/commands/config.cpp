// config.cpp -- `orca-cli config {set,unset,list}` subcommand wiring.
// Mirrors the shape of commands/plate.cpp and commands/object.cpp:
// per-subcommand option statics, callbacks dispatching into project_ops
// mutations, exit-code mapping via MutationExceptionMap + run_mutation
// from mutation_runner.hpp.
//
// For `config set` / `config unset` the exception map registers
// BadConfigError -> bad_config BEFORE the defaults so an unknown /
// malformed key maps to ExitCode::bad_config (exit 4) rather than the
// generic parse_failure (exit 3). BadConfigError extends
// std::runtime_error (not invalid_argument or out_of_range), so the
// MutationExceptionMap dispatch order already ensures it fires first.
#include "config.hpp"
#include "mutation_runner.hpp"

#include "../cli11/CLI11.hpp"
#include "../invariants.hpp"
#include "../io.hpp"
#include "../output.hpp"
#include "../project_ops.hpp"

#include <nlohmann/json.hpp>

#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace orca_cli::commands {

namespace {

// Look up the current serialized value for `key`. Used by `config set`
// and `config list` to surface the value alongside the key in the
// success message (human + JSON).
//
// For project_config we go through opt_serialize which already handles
// every option type. For per-object config we read via the
// ModelConfig::opt_serialize wrapper. Returns an empty string if the
// key isn't set (legitimate state on per-object config where the user
// just listed without setting).
std::string project_value(const ProjectState& s, const std::string& key)
{
    if (!s.project_config || !s.project_config->has(key)) return {};
    return s.project_config->opt_serialize(key);
}

std::string object_value(const Slic3r::ModelObject& obj, const std::string& key)
{
    if (!obj.config.has(key)) return {};
    return obj.config.opt_serialize(key);
}

// -- config set ------------------------------------------------------------

int do_config_set(const GlobalOpts& g,
                  const std::string& file,
                  const std::string& key,
                  const std::string& value,
                  const std::string& object_name)
{
    MutationExceptionMap em;
    em.on<BadConfigError>(ExitCode::bad_config);
    // out_of_range default = unknown_reference (set_object_config throws this
    // when the object name is unknown).
    const std::string msg = object_name.empty()
        ? ("set " + key + " = " + value)
        : ("set " + key + " = " + value + " on object '" + object_name + "'");
    return run_mutation(g, file, msg, em,
        [&](ProjectState& s) {
            if (object_name.empty()) set_project_config(s, key, value);
            else                     set_object_config(s, object_name, key, value);
        });
}

// -- config unset ----------------------------------------------------------

int do_config_unset(const GlobalOpts& g,
                    const std::string& file,
                    const std::string& key,
                    const std::string& object_name)
{
    MutationExceptionMap em;
    em.on<BadConfigError>(ExitCode::bad_config);
    const std::string msg = object_name.empty()
        ? ("unset " + key)
        : ("unset " + key + " on object '" + object_name + "'");
    return run_mutation(g, file, msg, em,
        [&](ProjectState& s) {
            if (object_name.empty()) unset_project_config(s, key);
            else                     unset_object_config(s, object_name, key);
        });
}

// -- config list -----------------------------------------------------------
//
// Read-only command. Rejects --output with usage_error (exit 1) the same
// way `plate list` and `object list` do.
//
// Without --object: emits project_config keys (filtered by
// changed_project_keys when --changed-only is set).
// With --object: emits the named object's per-object config keys.
// Per-object config only stores explicitly-set keys, so --changed-only
// is a no-op at the per-object scope -- we accept the flag for command
// shape uniformity but the listed set is unchanged.
int do_config_list(const GlobalOpts& g,
                   const std::string& file,
                   const std::string& object_name,
                   bool               changed_only)
{
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error,
                  "config list does not accept --output");
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

    // Build (key, value) rows for whichever scope was requested.
    struct Row { std::string key; std::string value; };
    std::vector<Row> rows;
    try {
        if (object_name.empty()) {
            const std::vector<std::string> keys = changed_only
                ? changed_project_keys(state)
                : state.project_config->keys();
            rows.reserve(keys.size());
            for (const auto& k : keys)
                rows.push_back({k, project_value(state, k)});
        } else {
            const auto* obj = find_object(state, object_name);
            if (!obj) {
                print_err(g, ExitCode::unknown_reference,
                          "object not found: " + object_name);
                return int(ExitCode::unknown_reference);
            }
            // object_config_keys would throw std::out_of_range if obj
            // were missing; we already checked above, but going through
            // the helper keeps the listing path consistent with the
            // mutation path (and exercises G6 indirectly when somebody
            // edits the helper later).
            const auto keys = object_config_keys(state, object_name);
            rows.reserve(keys.size());
            for (const auto& k : keys)
                rows.push_back({k, object_value(*obj, k)});
        }
    } catch (const std::out_of_range& e) {
        print_err(g, ExitCode::unknown_reference, e.what());
        return int(ExitCode::unknown_reference);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }

    if (g.json) {
        nlohmann::json data;
        auto& arr = data["keys"] = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({
                {"key",   r.key},
                {"value", r.value},
            });
        }
        print_ok(g, "listed " + std::to_string(rows.size()) + " config keys", data);
    } else {
        for (const auto& r : rows) {
            const std::string line = r.key + " = " + r.value + "\n";
            std::fputs(line.c_str(), stdout);
        }
        std::fflush(stdout);
    }
    return int(ExitCode::ok);
}

} // namespace

void register_config_subcmd(CLI::App& app, GlobalOpts& g)
{
    auto* cfg = app.add_subcommand("config", "project / per-object config get/set/unset");

    // Per-subcommand option statics. Same rationale as commands/plate.cpp
    // and commands/object.cpp -- CLI11 binds options by reference and we
    // want each parse to start from a clean slate.
    static std::string set_file, set_key, set_value, set_object;
    static std::string unset_file, unset_key, unset_object;
    static std::string list_file, list_object;
    static bool        list_changed_only = false;

    // -- config set --------------------------------------------------------
    auto* set_cmd = cfg->add_subcommand("set",
        "set a config key to a value (project-wide or per-object)");
    set_cmd->add_option("file",   set_file,   "input .3mf path")->required();
    set_cmd->add_option("--key",  set_key,
                        "config key (must exist in print_config_def)")->required();
    set_cmd->add_option("--value", set_value, "value to assign")->required();
    set_cmd->add_option("--object", set_object,
                        "(optional) per-object scope; writes to the named ModelObject's per-object config");
    set_cmd->add_option("--output", g.output,
                        "write result to this path instead of overwriting input");
    set_cmd->callback([&g]() {
        std::exit(do_config_set(g, set_file, set_key, set_value, set_object));
    });

    // -- config unset ------------------------------------------------------
    auto* unset_cmd = cfg->add_subcommand("unset",
        "remove a config key (project-wide or per-object)");
    unset_cmd->add_option("file",     unset_file, "input .3mf path")->required();
    unset_cmd->add_option("--key",    unset_key,  "config key to remove")->required();
    unset_cmd->add_option("--object", unset_object,
                          "(optional) per-object scope");
    unset_cmd->add_option("--output", g.output,
                          "write result to this path instead of overwriting input");
    unset_cmd->callback([&g]() {
        std::exit(do_config_unset(g, unset_file, unset_key, unset_object));
    });

    // -- config list -------------------------------------------------------
    auto* list_cmd = cfg->add_subcommand("list",
        "list config keys (project-wide or per-object)");
    list_cmd->add_option("file",     list_file, "input .3mf path")->required();
    list_cmd->add_option("--object", list_object,
                         "(optional) per-object scope");
    list_cmd->add_flag  ("--changed-only", list_changed_only,
                         "show only keys that differ from libslic3r defaults");
    // Same rationale as plate/object list: register --output here just so
    // we can reject it explicitly with usage_error rather than CLI11's
    // "unknown option" exit code.
    list_cmd->add_option("--output", g.output,
                         "(rejected on config list; mutating subcommands only)");
    list_cmd->callback([&g]() {
        std::exit(do_config_list(g, list_file, list_object, list_changed_only));
    });
}

} // namespace orca_cli::commands
