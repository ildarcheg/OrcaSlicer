#pragma once
#include "../globals.hpp"

namespace CLI { class App; }

namespace orca_cli::commands {

// Registers the read-only `inspect` subcommand. Mirrors the shape of
// commands/{plate,object,config}.cpp's `list` subcommands: load the
// project, walk Model + plates + project_config, emit a diagnostic
// dump to stdout. Rejects --output with usage_error (exit 1) the same
// way the other read-only listings do.
void register_inspect_subcmd(CLI::App& app, GlobalOpts& gopts);

} // namespace orca_cli::commands
