#pragma once
#include "../globals.hpp"

namespace CLI { class App; }

namespace orca_cli::commands {

// Registers the `config` subcommand and its three children
// (`set`, `unset`, `list`). Mirrors the pattern in commands/plate.cpp
// and commands/object.cpp: per-subcommand option statics, callbacks
// dispatching into project_ops mutations, exit-code mapping via the
// standard exception -> ExitCode chain.
void register_config_subcmd(CLI::App& app, GlobalOpts& gopts);

} // namespace orca_cli::commands
