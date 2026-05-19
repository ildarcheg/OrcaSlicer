#pragma once
#include "../globals.hpp"

namespace CLI { class App; }

namespace orca_cli::commands {

// Register the `plate` subcommand tree on the given CLI11 app:
//   orca-cli plate add    <file> --name <plate-name>  [--output <out>]
//   orca-cli plate remove <file> --name <plate-name>  [--output <out>]
//   orca-cli plate rename <file> --from <name> --to <name> [--output <out>]
//   orca-cli plate list   <file>
//
// Mutating subcommands write back over <file> by default, or to <out>
// when --output is provided. `list` rejects --output with usage_error.
void register_plate_subcmd(CLI::App& app, GlobalOpts& gopts);

} // namespace orca_cli::commands
