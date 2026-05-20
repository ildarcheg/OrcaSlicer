#pragma once
#include "globals.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

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

// Convenience for "ls"-style subcommands. Builds a JSON object with a single
// `list_name` array of items (each rendered via `to_json(row)`) and writes
// human-readable lines (each from `to_line(row)`) when JSON mode is off.
template <typename Row, typename ToJson, typename ToLine>
void emit_list_response(const GlobalOpts& opts,
                        std::string_view  list_name,
                        std::string_view  summary,
                        const std::vector<Row>& rows,
                        ToJson&& to_json,
                        ToLine&& to_line)
{
    if (opts.json) {
        nlohmann::json data;
        auto& arr = data[std::string(list_name)] = nlohmann::json::array();
        for (const auto& r : rows) arr.push_back(to_json(r));
        print_ok(opts, summary, data);
    } else {
        for (const auto& r : rows) {
            std::string line = to_line(r);
            if (line.empty() || line.back() != '\n') line.push_back('\n');
            std::fputs(line.c_str(), stdout);
        }
        std::fflush(stdout);
    }
}

} // namespace orca_cli
