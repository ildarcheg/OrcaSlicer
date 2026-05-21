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

void install_project_aux_subcmd    (CLI::App& /*project*/, GlobalOpts& /*g*/) {}

} // namespace orca_cli::commands
