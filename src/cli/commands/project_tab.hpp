#pragma once
#include "../globals.hpp"

namespace CLI { class App; }

namespace orca_cli::commands {

// Bodies for the three project-tab subverb families. Declarations also live
// in project_init.hpp so register_project_subcmd can dispatch — included
// here for any future caller that wants just one family.
void install_project_info_subcmd   (CLI::App& project, GlobalOpts& g);
void install_project_profile_subcmd(CLI::App& project, GlobalOpts& g);
void install_project_aux_subcmd    (CLI::App& project, GlobalOpts& g);

} // namespace orca_cli::commands
