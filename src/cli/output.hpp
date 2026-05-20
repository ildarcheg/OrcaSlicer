#pragma once
#include "globals.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

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

// Test-friendly envelope builders (no I/O). The body is a single line of
// dump()-formatted JSON; callers append '\n' before fputs.
std::string build_ok_envelope(std::string_view message, const nlohmann::json& data);
std::string build_err_envelope(ExitCode code, std::string_view message);

void print_ok(const GlobalOpts& opts, std::string_view message);
void print_ok(const GlobalOpts& opts, std::string_view message, const nlohmann::json& data);
void print_err(const GlobalOpts& opts, ExitCode code, std::string_view message);

} // namespace orca_cli
