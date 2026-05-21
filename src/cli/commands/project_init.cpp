#include "project_init.hpp"
#include "project_tab.hpp"

#include "../cli11/CLI11.hpp"
#include "../output.hpp"
#include "../io.hpp"
#include "../invariants.hpp"

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <cstdlib>
#include <exception>

namespace orca_cli::commands {

namespace fs = boost::filesystem;

namespace {

// Clone-and-mutate strategy for `project init`:
//   1. copy the template to a .tmp staging path next to the destination
//   2. load_project(tmp)  -- runs LoadModel | LoadConfig
//   3. save_project(out)  -- runs store_bbs_3mf, the thumbnail passthrough,
//                            and the runtime invariant guard
//   4. remove the .tmp
//
// Per the v1.1 design note: even though step (1) already produces a
// byte-identical copy, we route through load+save so that the resulting
// archive is exactly what every later mutation phase will also produce.
// That keeps the invariant guard meaningful from day one.
int do_project_init(const GlobalOpts&   g,
                    const std::string&  out,
                    const std::string&  tmpl)
{
    if (!fs::exists(tmpl)) {
        print_err(g, ExitCode::file_not_found, "template not found: " + tmpl);
        return int(ExitCode::file_not_found);
    }

    fs::path tmp = fs::path(out).string() + ".init-tmp";
    try {
        boost::system::error_code ec;
        fs::remove(tmp, ec);  // ignore failure; copy_file will report
        fs::copy_file(tmpl, tmp, fs::copy_options::overwrite_existing);

        auto state = load_project(tmp.string());

        // Cross-project audit P2: surface a broken input template at init
        // time instead of letting save_project's placeholder passthrough
        // silently auto-fix it. The user MUST know the template was bad
        // so they can regenerate it in OrcaSlicer GUI. We validate the
        // staging copy (the bytes load_project actually saw, avoiding a
        // TOCTOU swap on the original) but report against the user-
        // supplied path in any error message.
        verify_input_template_thumbnails(tmp.string(), tmpl);

        save_project(state, out);

        fs::remove(tmp, ec);  // best-effort cleanup
    } catch (const InvariantViolation& e) {
        boost::system::error_code ec;
        fs::remove(tmp, ec);
        print_err(g, ExitCode::invariant_violation, e.what());
        return int(ExitCode::invariant_violation);
    } catch (const std::exception& e) {
        boost::system::error_code ec;
        fs::remove(tmp, ec);
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }

    print_ok(g, "initialized project at " + out);
    return int(ExitCode::ok);
}

} // namespace

void install_project_init_subcmd(CLI::App& project, GlobalOpts& g)
{
    auto* init = project.add_subcommand(
        "init", "clone a reference 3mf into a new project");

    // CLI11 binds options to long-lived storage by reference. statics keep
    // these alive across the parse / callback boundary without forcing the
    // caller to manage their lifetime.
    static std::string out, tmpl;
    init->add_option("out", out, "destination .3mf path")->required();
    init->add_option("--template", tmpl, "reference .3mf to clone")->required();

    // CLI11 invokes the callback during parsing; the caller's main() returns
    // immediately after parse completes. std::exit so the exit code reflects
    // the command result rather than the parser's "everything parsed ok"
    // status.
    init->callback([&g]() {
        std::exit(do_project_init(g, out, tmpl));
    });
}

void register_project_subcmd(CLI::App& app, GlobalOpts& g)
{
    auto* project = app.add_subcommand("project", "project-level operations");
    install_project_init_subcmd   (*project, g);
    install_project_info_subcmd   (*project, g);
    install_project_profile_subcmd(*project, g);
    install_project_aux_subcmd    (*project, g);
}

} // namespace orca_cli::commands
