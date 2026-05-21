#pragma once
#include "../globals.hpp"

namespace CLI { class App; }

namespace orca_cli::commands {

// Creates the `project` parent subcommand and installs all subverb families.
// Called once from main.cpp.
void register_project_subcmd(CLI::App& app, GlobalOpts& g);

// Installs ONE subverb family under an existing `project` parent. Each leaf
// installer is responsible for its own subverb registrations + callbacks.
// Used internally by register_project_subcmd; exposed here so project_tab.cpp
// can be a separate translation unit.
void install_project_init_subcmd   (CLI::App& project, GlobalOpts& g);
void install_project_info_subcmd   (CLI::App& project, GlobalOpts& g);
void install_project_profile_subcmd(CLI::App& project, GlobalOpts& g);
void install_project_aux_subcmd    (CLI::App& project, GlobalOpts& g);

} // namespace orca_cli::commands
