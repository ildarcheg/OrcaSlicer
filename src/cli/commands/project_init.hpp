#pragma once
#include "../globals.hpp"

namespace CLI { class App; }

namespace orca_cli::commands {

// Register the `project` subcommand tree on the given CLI11 app.
// In Phase 1 this registers a single leaf command:
//   orca-cli project init <out> --template <ref>
// which clones the reference 3mf via the standard load_project /
// save_project flow (so the invariant guard runs end-to-end on the
// produced archive).
void register_project_subcmd(CLI::App& app, GlobalOpts& gopts);

} // namespace orca_cli::commands
