#pragma once
#include <string>
#include <string_view>
#include "globals.hpp"

namespace orca_cli {

enum class ExitCode : int {
    ok                  = 0,
    usage_error         = 1,
    file_not_found      = 2,
    parse_failure       = 3,
    bad_config          = 4,
    duplicate_name      = 5,
    unknown_reference   = 6,
    invalid_state       = 7,
    invariant_violation = 8,
    placement_failure   = 9,
};

const char* code_name(ExitCode c);

void print_ok (const GlobalOpts& opts, std::string_view message, std::string_view data_json = {});
void print_err(const GlobalOpts& opts, ExitCode code, std::string_view message);

// Escapes a string for safe embedding inside a JSON value.
std::string escape_json(std::string_view s);

} // namespace orca_cli
