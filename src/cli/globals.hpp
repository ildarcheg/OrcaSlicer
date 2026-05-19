#pragma once
#include <optional>
#include <string>

namespace CLI { class App; }

namespace orca_cli {

struct GlobalOpts {
    bool                       json    = false;
    bool                       verbose = false;
    std::optional<std::string> output;
};

void register_global_flags(CLI::App& app, GlobalOpts& opts);

} // namespace orca_cli
