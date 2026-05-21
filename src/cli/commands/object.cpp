// object.cpp -- `orca-cli object {add,remove,list}` subcommand wiring.
// Mirrors the shape of commands/plate.cpp: per-subcommand option statics,
// callbacks dispatching into project_ops mutations, exit-code mapping
// via MutationExceptionMap + run_mutation from mutation_runner.hpp.
#include "object.hpp"
#include "mutation_runner.hpp"
#include "object_parse_vec3.hpp"

#include "../cli11/CLI11.hpp"
#include "../invariants.hpp"
#include "../io.hpp"
#include "../output.hpp"
#include "../project_ops.hpp"

#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace orca_cli::commands {

namespace fs = boost::filesystem;

namespace {

struct AddObjectRawOpts {
    std::string file;
    std::string plate;
    std::string stl;
    int         count           = 1;
    std::string name;
    std::string translate_str;
    std::string rotate_str;
    std::string scale_str;
    int         filament_slot   = 0; // 0 = unset
};

int do_object_add(const GlobalOpts& g, const AddObjectRawOpts& o)
{
    if (int rc = check_input_exists(g, o.file); rc != int(ExitCode::ok))
        return rc;
    if (!fs::exists(o.stl)) {
        print_err(g, ExitCode::file_not_found, "stl not found: " + o.stl);
        return int(ExitCode::file_not_found);
    }

    // Parse transform flags up-front so a bad value is reported as a
    // usage_error before we touch the archive. CLI11 leaves the strings
    // empty when the flag wasn't supplied.
    std::optional<Slic3r::Vec3d> translate, rotate, scale;

    // --translate: 2 or 3 components.
    if (!o.translate_str.empty()) {
        auto r = parse_vec3(o.translate_str);
        if (!r || (r->component_count != 2 && r->component_count != 3)) {
            print_err(g, ExitCode::usage_error,
                      "invalid --translate value '" + o.translate_str +
                      "' (expected x,y or x,y,z)");
            return int(ExitCode::usage_error);
        }
        translate = r->values;
    }

    // --rotate: exactly 3 components.
    if (!o.rotate_str.empty()) {
        auto r = parse_vec3(o.rotate_str);
        if (!r || r->component_count != 3) {
            print_err(g, ExitCode::usage_error,
                      "invalid --rotate value '" + o.rotate_str +
                      "' (expected ax,ay,az in radians)");
            return int(ExitCode::usage_error);
        }
        rotate = r->values;
    }

    // --scale: scalar (1 component) or 3 components.
    if (!o.scale_str.empty()) {
        auto r = parse_vec3(o.scale_str);
        if (!r || (r->component_count != 1 && r->component_count != 3)) {
            print_err(g, ExitCode::usage_error,
                      "invalid --scale value '" + o.scale_str +
                      "' (expected s or sx,sy,sz)");
            return int(ExitCode::usage_error);
        }
        scale = r->values;
    }

    MutationExceptionMap em;
    em.on<PlacementFailure>(ExitCode::placement_failure);
    // invalid_argument default = duplicate_name (unchanged)
    // out_of_range default = unknown_reference (unchanged)

    return run_mutation(g, o.file,
        "added object from '" + o.stl + "' to plate '" + o.plate + "'", em,
        [&](ProjectState& s) {
            AddObjectParams p;
            p.plate_name  = o.plate;
            p.stl_path    = o.stl;
            p.object_name = o.name;
            p.count       = o.count;
            p.translate   = translate;
            p.rotate      = rotate;
            p.scale       = scale;
            // CLI11 stores filament_slot as a plain int with default 0; we
            // treat 0 as "unset" because libslic3r's extruder index is 1-based
            // (the GUI's filament panel labels start at 1, not 0). Anything
            // else (negative or positive) is forwarded so the validation in
            // set_object_filament reports the same error the user would see
            // from `object set-filament`.
            if (o.filament_slot != 0) p.filament_slot = o.filament_slot;
            add_object(s, p);
        });
}

int do_object_set_filament(const GlobalOpts& g,
                           const std::string& file,
                           const std::string& name,
                           int                slot,
                           const std::string& part)
{
    // Both "object not found" and "slot out of range" surface as
    // std::out_of_range from set_object_filament; both map to
    // unknown_reference per the P5 plan. Default MutationExceptionMap
    // maps out_of_range -> unknown_reference, which is exactly what we need.
    // "part not found" also throws std::out_of_range and maps the same way.
    MutationExceptionMap em;
    std::optional<std::string> part_opt =
        part.empty() ? std::nullopt : std::optional<std::string>(part);
    std::string ok_msg = "set filament " + std::to_string(slot) +
                         " on object '" + name + "'" +
                         (part.empty() ? "" : (" part='" + part + "'"));
    return run_mutation(g, file, ok_msg, em,
        [&](ProjectState& s) { set_object_filament(s, name, slot, part_opt); });
}

int do_object_remove(const GlobalOpts& g,
                     const std::string& file,
                     const std::string& name)
{
    MutationExceptionMap em;
    return run_mutation(g, file, "removed object '" + name + "'", em,
        [&](ProjectState& s) { remove_object(s, name); });
}

int do_object_list(const GlobalOpts& g,
                   const std::string& file,
                   const std::string& plate_filter)
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

    // Cross-project audit P3: optional --plate P filter (spec § 4.3 line 154).
    // Validate the plate name up front so a typo surfaces as exit 6 instead
    // of an empty list that the user could mistake for "no objects".
    if (!plate_filter.empty()) {
        bool found = false;
        for (const auto& pd : state.plates) {
            if (pd->plate_name == plate_filter) { found = true; break; }
        }
        if (!found) {
            print_err(g, ExitCode::unknown_reference,
                      "plate not found: " + plate_filter);
            return int(ExitCode::unknown_reference);
        }
    }

    // Build (object_name, plate_name) tuples by walking
    // PlateData::objects_and_instances. An object on no plate (rare but
    // possible after `object remove` edge cases or weird input 3mfs) is
    // still emitted with an empty plate name so the user can see it --
    // unless --plate P is in effect, in which case we only emit rows
    // whose plate matches P.
    struct Row { std::string object; std::string plate; };
    std::vector<Row> rows;
    std::vector<bool> on_plate(state.model->objects.size(), false);
    for (const auto& pd : state.plates) {
        for (const auto& kv : pd->objects_and_instances) {
            int oi = kv.first;
            if (oi < 0 || oi >= int(state.model->objects.size())) continue;
            if (on_plate[oi]) continue; // one row per (object, first-plate)
            on_plate[oi] = true;
            if (!plate_filter.empty() && pd->plate_name != plate_filter) continue;
            rows.push_back({state.model->objects[oi]->name, pd->plate_name});
        }
    }
    // Objects that aren't on any plate -- surface them only when no filter
    // is in effect, since by definition they don't belong to the named plate.
    if (plate_filter.empty()) {
        for (size_t i = 0; i < state.model->objects.size(); ++i) {
            if (on_plate[i]) continue;
            rows.push_back({state.model->objects[i]->name, std::string{}});
        }
    }

    emit_list_response(g, "objects",
        "listed " + std::to_string(rows.size()) + " objects",
        rows,
        [](const Row& r) {
            return nlohmann::json{{"name",  r.object},
                                  {"plate", r.plate}};
        },
        [](const Row& r) {
            return "object: " + r.object + " on plate " + r.plate;
        });
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
    static std::string ls_plate;
    // P5: state for `object set-filament`.
    static std::string sf_file, sf_name, sf_part;
    static int         sf_slot = 0;
    // Phase 8: state for `object split-to-parts`.
    static std::string split_file, split_name;
    // Phase 9: state for `object merge-parts`. `merge_filament` has no
    // "unset" sentinel -- we use CLI11's `count("--filament") > 0` in
    // the callback to distinguish "user passed --filament" from "user
    // did not pass --filament". This way `--filament 0` reaches the
    // range-check correctly (and is rejected as out-of-range) instead
    // of being silently swallowed as "no override".
    static std::string merge_file, merge_name, merge_parts, merge_into;
    static int         merge_filament = 0;

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
        AddObjectRawOpts opts;
        opts.file          = add_file;
        opts.plate         = add_plate;
        opts.stl           = add_stl;
        opts.count         = add_count;
        opts.name          = add_name;
        opts.translate_str = add_translate;
        opts.rotate_str    = add_rotate;
        opts.scale_str     = add_scale;
        opts.filament_slot = add_filament;
        std::exit(do_object_add(g, opts));
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
    setf->add_option("--part", sf_part,
                     "name of the volume (post-split part) to target; omit for object-level config");
    setf->add_option("--output", g.output,
                     "write result to this path instead of overwriting input");
    setf->callback([&g]() {
        std::exit(do_object_set_filament(g, sf_file, sf_name, sf_slot, sf_part));
    });

    // -- object split-to-parts (Phase 8) ----------------------------------
    // Decompose the named object's single mesh into multiple ModelVolumes,
    // one per connected component. The object keeps its name, plate
    // assignment, and instance count.
    //
    // Exception mapping:
    //   std::out_of_range     (unknown object)   -> ExitCode::unknown_reference
    //   std::invalid_argument (wrong # volumes,
    //                          not MODEL_PART,
    //                          or only 1 component) -> ExitCode::invalid_state
    auto* split = obj->add_subcommand("split-to-parts",
        "decompose an object's single mesh into multiple parts (one per connected component)");
    split->add_option("file", split_file, "input .3mf path")->required();
    split->add_option("--name", split_name, "name of the object to split")->required();
    split->add_option("--output", g.output,
        "write result to this path instead of overwriting input");
    split->callback([&g]() {
        MutationExceptionMap em;
        em.set_default_invalid_argument(ExitCode::invalid_state)
          .set_default_out_of_range(ExitCode::unknown_reference);
        std::exit(run_mutation(g, split_file,
            "split object '" + split_name + "' into parts", em,
            [](ProjectState& s) { split_object_to_parts(s, split_name); }));
    });

    // -- object merge-parts (Phase 9) -------------------------------------
    // Combine a subset of an object's ModelVolumes into a single merged
    // ModelVolume. Natural inverse of split-to-parts.
    //
    // Exception mapping:
    //   DuplicateNameError    -> ExitCode::duplicate_name  (exit 5, case 8)
    //   std::out_of_range     -> ExitCode::unknown_reference (exit 6,
    //                                cases 5, 6, 7)
    //   std::invalid_argument -> ExitCode::invalid_state   (exit 7,
    //                                cases 10, 11, 12, 13, 14)
    auto* merge = obj->add_subcommand("merge-parts",
        "combine a subset of an object's parts into a single merged part");
    merge->add_option("file", merge_file, "input .3mf path")->required();
    merge->add_option("--name", merge_name,
                      "name of the parent object")->required();
    merge->add_option("--parts", merge_parts,
                      "comma-separated list of source volume names (>=2)")
        ->required();
    merge->add_option("--into", merge_into,
                      "name for the resulting merged volume")->required();
    auto* merge_filament_opt = merge->add_option("--filament", merge_filament,
                      "explicit filament slot to assign to the merged volume "
                      "(1-based); required when sources have differing extruders");
    merge->add_option("--output", g.output,
                      "write result to this path instead of overwriting input");
    merge->callback([&g, merge_filament_opt]() {
        // Section 3 precedence step 1: parse-level validation (usage_error).
        if (merge_into.empty()) {
            print_err(g, ExitCode::usage_error,
                "merge-parts --into must be non-empty");
            std::exit(int(ExitCode::usage_error));
        }
        // Split the comma list. Empty -> case 1; size==1 -> case 2;
        // duplicates -> case 3. All map to usage_error.
        std::vector<std::string> parts;
        {
            std::string cur;
            for (char c : merge_parts) {
                if (c == ',') {
                    if (!cur.empty()) parts.push_back(cur);
                    cur.clear();
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) parts.push_back(cur);
        }
        if (parts.empty()) {
            print_err(g, ExitCode::usage_error,
                "merge-parts --parts must be non-empty (>=2 source names)");
            std::exit(int(ExitCode::usage_error));
        }
        if (parts.size() < 2) {
            print_err(g, ExitCode::usage_error,
                "merge-parts requires >=2 source parts");
            std::exit(int(ExitCode::usage_error));
        }
        {
            std::set<std::string> seen;
            for (const auto& n : parts) {
                if (!seen.insert(n).second) {
                    print_err(g, ExitCode::usage_error,
                        "merge-parts --parts contains duplicate name '" +
                        n + "'");
                    std::exit(int(ExitCode::usage_error));
                }
            }
        }

        // Distinguish "user passed --filament" from "user did not pass it"
        // via CLI11's count(); avoids the `int == 0` sentinel ambiguity
        // (--filament 0 must reach the range check and be rejected, not
        // silently treated as "no override").
        std::optional<int> filament_override;
        if (merge_filament_opt->count() > 0) {
            filament_override = merge_filament;
        }

        MutationExceptionMap em;
        em.on<DuplicateNameError>(ExitCode::duplicate_name)
          .set_default_invalid_argument(ExitCode::invalid_state)
          .set_default_out_of_range(ExitCode::unknown_reference);
        std::exit(run_mutation(g, merge_file,
            "merge parts of object '" + merge_name + "' into '" +
                merge_into + "'", em,
            [parts, filament_override](ProjectState& s) {
                merge_object_parts(s, merge_name, parts,
                                   merge_into, filament_override);
            }));
    });

    // -- object list -------------------------------------------------------
    auto* ls = obj->add_subcommand("list", "list objects in a project");
    ls->add_option("file", ls_file, "input .3mf path")->required();
    // Cross-project audit P3 (spec § 4.3 line 154): optional --plate filter.
    // Unknown plate name -> exit 6 (unknown_reference); empty string means
    // "no filter, list all" so the test harness defaults work unchanged.
    ls->add_option("--plate", ls_plate,
                   "(optional) filter to objects on the named plate");
    // See commands/plate.cpp for the rationale on registering --output
    // here just to reject it explicitly with usage_error rather than the
    // CLI11 "unknown option" exit code.
    ls->add_option("--output", g.output,
                   "(rejected on object list; mutating subcommands only)");
    ls->callback([&g]() {
        std::exit(do_object_list(g, ls_file, ls_plate));
    });
}

} // namespace orca_cli::commands
