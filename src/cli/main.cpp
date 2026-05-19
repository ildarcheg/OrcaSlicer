#include "cli11/CLI11.hpp"
#include "globals.hpp"
#include "output.hpp"
#include "commands/project_init.hpp"
#include "commands/plate.hpp"
#include "commands/object.hpp"
#include "commands/config.hpp"
#include <cstdlib>
#include <cstdio>

namespace { constexpr const char* kVersion = "orca-cli 0.1.0-dev"; }

int main(int argc, char** argv)
{
    CLI::App app{"orca-cli - 3MF composer for OrcaSlicer"};
    app.set_version_flag("--version", kVersion);
    app.require_subcommand(0, 1);

    orca_cli::GlobalOpts opts;
    orca_cli::register_global_flags(app, opts);
    orca_cli::commands::register_project_subcmd(app, opts);
    orca_cli::commands::register_plate_subcmd  (app, opts);
    orca_cli::commands::register_object_subcmd (app, opts);
    orca_cli::commands::register_config_subcmd (app, opts);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (app.get_subcommands().empty()) {
        std::fputs(app.help().c_str(), stderr);
        return static_cast<int>(orca_cli::ExitCode::usage_error);
    }
    return static_cast<int>(orca_cli::ExitCode::ok);
}
