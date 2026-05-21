#include "project_tab.hpp"
#include "mutation_runner.hpp"
#include "../cli11/CLI11.hpp"
#include "../io.hpp"
#include "../output.hpp"
#include "../project_tab_ops.hpp"

#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace orca_cli::commands {

namespace {

// -- info show -------------------------------------------------------------

int do_info_show(const GlobalOpts& g, const std::string& file)
{
    // Belt-and-suspenders: --output isn't registered on this subcommand
    // (CLI11 would reject it as "unknown option" -> usage_error), but if a
    // future refactor re-exposes it accidentally we surface a clear
    // diagnostic instead of silently ignoring the flag.
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error, "project info show does not accept --output");
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok)) return rc;

    ProjectState state;
    try {
        state = load_project(file);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    auto v = info_view(state);
    if (g.json) {
        nlohmann::json data{
            {"title",       v.title},
            {"description", v.description},
            {"license",     v.license},
            {"copyright",   v.copyright},
            {"cover",       v.cover},
            {"origin",      v.origin},
        };
        print_ok(g, "info", data);
    } else {
        std::fputs(("title:       " + v.title       + "\n").c_str(), stdout);
        std::fputs(("description: " + v.description + "\n").c_str(), stdout);
        std::fputs(("license:     " + v.license     + "\n").c_str(), stdout);
        std::fputs(("copyright:   " + v.copyright   + "\n").c_str(), stdout);
        std::fputs(("cover:       " + v.cover       + "\n").c_str(), stdout);
        std::fputs(("origin:      " + v.origin      + "\n").c_str(), stdout);
        std::fflush(stdout);
    }
    return int(ExitCode::ok);
}

// -- info set --------------------------------------------------------------

int do_info_set(const GlobalOpts& g, const std::string& file, const InfoSetParams& p)
{
    if (!any_field_set(p)) {
        print_err(g, ExitCode::usage_error,
                  "project info set requires at least one of --title/--description/"
                  "--license/--copyright/--cover");
        return int(ExitCode::usage_error);
    }
    MutationExceptionMap em;
    em.on<BadCoverImage>(ExitCode::bad_config);
    return run_mutation(g, file, "info set applied", em,
        [&](ProjectState& s) { info_set(s, p); });
}

// -- info clear ------------------------------------------------------------

int do_info_clear(const GlobalOpts& g, const std::string& file,
                  const std::vector<std::string>& fields)
{
    MutationExceptionMap em;
    em.on<InvalidField>(ExitCode::bad_config);
    return run_mutation(g, file, "info cleared", em,
        [&](ProjectState& s) { info_clear(s, fields); });
}

// Split a comma-separated string into trimmed tokens. Empty tokens dropped.
std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// -- profile show -----------------------------------------------------------

int do_profile_show(const GlobalOpts& g, const std::string& file)
{
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error, "project profile show does not accept --output");
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok)) return rc;

    ProjectState state;
    try {
        state = load_project(file);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    auto v = profile_view(state);
    if (g.json) {
        nlohmann::json data{
            {"title",       v.title},
            {"description", v.description},
            {"cover",       v.cover},
            {"user_id",     v.user_id},
            {"user_name",   v.user_name},
        };
        print_ok(g, "profile", data);
    } else {
        std::fputs(("title:       " + v.title       + "\n").c_str(), stdout);
        std::fputs(("description: " + v.description + "\n").c_str(), stdout);
        std::fputs(("cover:       " + v.cover       + "\n").c_str(), stdout);
        std::fputs(("user_id:     " + v.user_id     + "\n").c_str(), stdout);
        std::fputs(("user_name:   " + v.user_name   + "\n").c_str(), stdout);
        std::fflush(stdout);
    }
    return int(ExitCode::ok);
}

// -- profile set ------------------------------------------------------------

int do_profile_set(const GlobalOpts& g, const std::string& file, const ProfileSetParams& p)
{
    if (!any_field_set(p)) {
        print_err(g, ExitCode::usage_error,
                  "project profile set requires at least one of --title/--description/--cover");
        return int(ExitCode::usage_error);
    }
    MutationExceptionMap em;
    em.on<BadCoverImage>(ExitCode::bad_config);
    return run_mutation(g, file, "profile set applied", em,
        [&](ProjectState& s) { profile_set(s, p); });
}

// -- profile clear ----------------------------------------------------------

int do_profile_clear(const GlobalOpts& g, const std::string& file,
                     const std::vector<std::string>& fields)
{
    MutationExceptionMap em;
    em.on<InvalidField>(ExitCode::bad_config);
    return run_mutation(g, file, "profile cleared", em,
        [&](ProjectState& s) { profile_clear(s, fields); });
}

} // namespace

void install_project_info_subcmd(CLI::App& project, GlobalOpts& g)
{
    auto* info = project.add_subcommand("info", "model-level metadata (Project tab)");

    // -- show --------------------------------------------------------------
    auto* show = info->add_subcommand("show", "print model metadata fields");
    static std::string show_file;
    show->add_option("file", show_file, "input .3mf path")->required();
    // Read-only verb: --output is intentionally NOT registered here so
    // CLI11 rejects it as an unknown option (cleaner --help; the runtime
    // guard in do_info_show is defense-in-depth).
    show->callback([&g]() { std::exit(do_info_show(g, show_file)); });

    // -- set ---------------------------------------------------------------
    auto* set = info->add_subcommand("set", "set one or more model metadata fields");
    static std::string set_file;
    static std::string s_title, s_desc, s_license, s_copyright, s_cover;
    static bool h_title=false, h_desc=false, h_license=false, h_copyright=false, h_cover=false;
    set->add_option("file", set_file, "input .3mf path")->required();
    auto* o_t = set->add_option("--title",       s_title,     "model title")->each([&](const std::string&){h_title=true;});
    auto* o_d = set->add_option("--description", s_desc,      "model description")->each([&](const std::string&){h_desc=true;});
    auto* o_l = set->add_option("--license",     s_license,   "model license")->each([&](const std::string&){h_license=true;});
    auto* o_c = set->add_option("--copyright",   s_copyright, "model copyright (JSON array string)")->each([&](const std::string&){h_copyright=true;});
    auto* o_v = set->add_option("--cover",       s_cover,     "PNG cover image (file path)")->each([&](const std::string&){h_cover=true;});
    (void)o_t; (void)o_d; (void)o_l; (void)o_c; (void)o_v;
    set->add_option("--output", g.output,
                    "write result to this path instead of overwriting input");
    set->callback([&g]() {
        InfoSetParams p;
        if (h_title)     p.title       = s_title;
        if (h_desc)      p.description = s_desc;
        if (h_license)   p.license     = s_license;
        if (h_copyright) p.copyright   = s_copyright;
        if (h_cover)     p.cover       = boost::filesystem::path(s_cover);
        int rc = do_info_set(g, set_file, p);
        // Reset flags for the next parse (statics live forever).
        h_title=h_desc=h_license=h_copyright=h_cover=false;
        std::exit(rc);
    });

    // -- clear -------------------------------------------------------------
    auto* clear = info->add_subcommand("clear", "null one or more model metadata fields");
    static std::string clear_file, clear_fields_csv;
    clear->add_option("file", clear_file, "input .3mf path")->required();
    clear->add_option("--field", clear_fields_csv,
                      "comma-separated field names (title,description,license,copyright,cover)")->required();
    clear->add_option("--output", g.output,
                      "write result to this path instead of overwriting input");
    clear->callback([&g]() {
        auto fields = split_csv(clear_fields_csv);
        std::exit(do_info_clear(g, clear_file, fields));
    });
}

void install_project_profile_subcmd(CLI::App& project, GlobalOpts& g)
{
    auto* prof = project.add_subcommand("profile", "print-profile metadata (Project tab)");

    // -- show ----------------------------------------------------------------
    auto* show = prof->add_subcommand("show", "print profile metadata fields");
    static std::string show_file;
    show->add_option("file", show_file, "input .3mf path")->required();
    // Read-only verb: --output intentionally NOT registered (see info show).
    show->callback([&g]() { std::exit(do_profile_show(g, show_file)); });

    // -- set -----------------------------------------------------------------
    auto* set = prof->add_subcommand("set", "set one or more profile metadata fields");
    static std::string set_file, s_title, s_desc, s_cover;
    static bool h_title=false, h_desc=false, h_cover=false;
    set->add_option("file", set_file, "input .3mf path")->required();
    set->add_option("--title",       s_title, "profile title")->each([&](const std::string&){h_title=true;});
    set->add_option("--description", s_desc,  "profile description")->each([&](const std::string&){h_desc=true;});
    set->add_option("--cover",       s_cover, "PNG cover image (file path)")->each([&](const std::string&){h_cover=true;});
    set->add_option("--output", g.output,
                    "write result to this path instead of overwriting input");
    set->callback([&g]() {
        ProfileSetParams p;
        if (h_title) p.title       = s_title;
        if (h_desc)  p.description = s_desc;
        if (h_cover) p.cover       = boost::filesystem::path(s_cover);
        int rc = do_profile_set(g, set_file, p);
        h_title=h_desc=h_cover=false;
        std::exit(rc);
    });

    // -- clear ---------------------------------------------------------------
    auto* clear = prof->add_subcommand("clear", "null one or more profile metadata fields");
    static std::string clear_file, clear_fields_csv;
    clear->add_option("file", clear_file, "input .3mf path")->required();
    clear->add_option("--field", clear_fields_csv,
                      "comma-separated field names (title,description,cover)")->required();
    clear->add_option("--output", g.output,
                      "write result to this path instead of overwriting input");
    clear->callback([&g]() {
        auto fields = split_csv(clear_fields_csv);
        std::exit(do_profile_clear(g, clear_file, fields));
    });
}

namespace {

// Map --folder string to AuxFolder enum, or throw a CLI11 ValidationError.
AuxFolder parse_folder(const std::string& s) {
    if (s == "pictures")       return AuxFolder::pictures;
    if (s == "bom")            return AuxFolder::bom;
    if (s == "assembly-guide") return AuxFolder::assembly_guide;
    if (s == "others")         return AuxFolder::others;
    throw CLI::ValidationError("--folder",
        "must be one of: pictures, bom, assembly-guide, others (got '" + s + "')");
}

int do_aux_list(const GlobalOpts& g, const std::string& file)
{
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error, "project aux list does not accept --output");
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok)) return rc;

    ProjectState state;
    try {
        state = load_project(file);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    auto entries = aux_list(state);
    if (g.json) {
        nlohmann::json data{
            {"pictures",       nlohmann::json::array()},
            {"bom",            nlohmann::json::array()},
            {"assembly_guide", nlohmann::json::array()},
            {"others",         nlohmann::json::array()},
        };
        for (const auto& e : entries)
            data[folder_json_key(e.folder)].push_back({{"name", e.name}, {"size", e.size}});
        print_ok(g, "aux", data);
    } else {
        for (const auto& e : entries) {
            std::string line = std::string(folder_flag(e.folder)) + "/" + e.name
                             + "  " + std::to_string(e.size) + " B\n";
            std::fputs(line.c_str(), stdout);
        }
        std::fflush(stdout);
    }
    return int(ExitCode::ok);
}

int do_aux_add(const GlobalOpts& g, const std::string& file,
               const std::string& folder, const std::string& src,
               const std::string& name, bool force)
{
    AuxFolder f;
    try { f = parse_folder(folder); }
    catch (const CLI::ValidationError& e) {
        print_err(g, ExitCode::usage_error, e.what());
        return int(ExitCode::usage_error);
    }
    MutationExceptionMap em;
    em.on<BadAuxFile>(ExitCode::file_not_found);
    em.on<AuxNameError>(ExitCode::bad_config);
    em.on<AuxCollisionError>(ExitCode::duplicate_name);

    AuxAddParams p;
    p.folder = f;
    p.file   = boost::filesystem::path(src);
    if (!name.empty()) p.name = name;
    p.force  = force;
    return run_mutation(g, file, "aux added", em,
        [&](ProjectState& s) { aux_add(s, p); });
}

int do_aux_remove(const GlobalOpts& g, const std::string& file,
                  const std::string& folder, const std::string& name)
{
    AuxFolder f;
    try { f = parse_folder(folder); }
    catch (const CLI::ValidationError& e) {
        print_err(g, ExitCode::usage_error, e.what());
        return int(ExitCode::usage_error);
    }
    MutationExceptionMap em;  // default out_of_range -> unknown_reference
    return run_mutation(g, file, "aux removed", em,
        [&](ProjectState& s) { aux_remove(s, f, name); });
}

int do_aux_export(const GlobalOpts& g, const std::string& file,
                  const std::string& folder, const std::string& name,
                  const std::string& to)
{
    if (g.output.has_value()) {
        print_err(g, ExitCode::usage_error, "project aux export does not accept --output (use --to)");
        return int(ExitCode::usage_error);
    }
    AuxFolder f;
    try { f = parse_folder(folder); }
    catch (const CLI::ValidationError& e) {
        print_err(g, ExitCode::usage_error, e.what());
        return int(ExitCode::usage_error);
    }
    if (int rc = check_input_exists(g, file); rc != int(ExitCode::ok)) return rc;

    ProjectState state;
    try {
        state = load_project(file);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    try {
        aux_export(state, f, name, boost::filesystem::path(to));
    } catch (const std::out_of_range& e) {
        print_err(g, ExitCode::unknown_reference, e.what());
        return int(ExitCode::unknown_reference);
    } catch (const std::invalid_argument& e) {
        print_err(g, ExitCode::bad_config, e.what());
        return int(ExitCode::bad_config);
    } catch (const std::exception& e) {
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }
    print_ok(g, "aux exported");
    return int(ExitCode::ok);
}

} // namespace (aux helpers anon block)

void install_project_aux_subcmd(CLI::App& project, GlobalOpts& g)
{
    auto* aux = project.add_subcommand("aux", "auxiliary file attachments (Project tab)");

    auto* list = aux->add_subcommand("list", "list aux entries by bucket");
    static std::string list_file;
    list->add_option("file", list_file, "input .3mf path")->required();
    // Read-only verb: --output intentionally NOT registered (see info show).
    list->callback([&g]() { std::exit(do_aux_list(g, list_file)); });

    auto* add = aux->add_subcommand("add", "attach a file to an aux bucket");
    static std::string add_file, add_folder, add_src, add_name;
    static bool add_force = false;
    add->add_option("input", add_file, "input .3mf path")->required();
    add->add_option("--folder", add_folder,
                    "pictures | bom | assembly-guide | others")->required();
    add->add_option("--file",   add_src,  "source file on disk")->required();
    add->add_option("--name",   add_name, "override in-3mf basename");
    add->add_flag  ("--force",  add_force,
                    "overwrite an existing aux entry with the same name");
    add->add_option("--output", g.output,
                    "write result to this path instead of overwriting input");
    add->callback([&g]() {
        int rc = do_aux_add(g, add_file, add_folder, add_src, add_name, add_force);
        add_force = false; add_name.clear();
        std::exit(rc);
    });

    auto* rem = aux->add_subcommand("remove", "delete an aux entry");
    static std::string rem_file, rem_folder, rem_name;
    rem->add_option("file", rem_file, "input .3mf path")->required();
    rem->add_option("--folder", rem_folder,
                    "pictures | bom | assembly-guide | others")->required();
    rem->add_option("--name",   rem_name, "in-3mf basename to remove")->required();
    rem->add_option("--output", g.output,
                    "write result to this path instead of overwriting input");
    rem->callback([&g]() { std::exit(do_aux_remove(g, rem_file, rem_folder, rem_name)); });

    auto* exp = aux->add_subcommand("export", "copy an aux entry out to disk");
    static std::string exp_file, exp_folder, exp_name, exp_to;
    exp->add_option("file", exp_file, "input .3mf path")->required();
    exp->add_option("--folder", exp_folder,
                    "pictures | bom | assembly-guide | others")->required();
    exp->add_option("--name",   exp_name, "in-3mf basename to export")->required();
    exp->add_option("--to",     exp_to,   "destination file or directory")->required();
    exp->callback([&g]() { std::exit(do_aux_export(g, exp_file, exp_folder, exp_name, exp_to)); });
}

} // namespace orca_cli::commands
