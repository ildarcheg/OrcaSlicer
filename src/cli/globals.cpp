#include "globals.hpp"
#include "cli11/CLI11.hpp"

namespace orca_cli {

void register_global_flags(CLI::App& app, GlobalOpts& opts)
{
    app.add_flag("--json",    opts.json,    "Emit machine-readable JSON on stdout");
    app.add_flag("--verbose", opts.verbose, "Verbose per-stage logging on stderr");
}

} // namespace orca_cli
