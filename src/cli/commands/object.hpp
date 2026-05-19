#pragma once
#include "../globals.hpp"

namespace CLI { class App; }

namespace orca_cli::commands {

// Register the `object` subcommand tree on the given CLI11 app:
//   orca-cli object add    <file> --plate <P> --stl <S> [--count N] [--name M] [--output O]
//   orca-cli object remove <file> --name <M> [--output O]
//   orca-cli object list   <file>
//
// `add` loads the STL, stamps source attribution on every volume (Bug C
// defense), and places `count` instances on the named plate via the
// deterministic per-plate grid. `list` rejects --output with usage_error.
void register_object_subcmd(CLI::App& app, GlobalOpts& gopts);

} // namespace orca_cli::commands
