#include "output.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>

namespace orca_cli {

const char* code_name(ExitCode c)
{
    switch (c) {
        case ExitCode::ok:                  return "ok";
        case ExitCode::usage_error:         return "usage_error";
        case ExitCode::file_not_found:      return "file_not_found";
        case ExitCode::parse_failure:       return "parse_failure";
        case ExitCode::bad_config:          return "bad_config";
        case ExitCode::duplicate_name:      return "duplicate_name";
        case ExitCode::unknown_reference:   return "unknown_reference";
        case ExitCode::invalid_state:       return "invalid_state";
        case ExitCode::invariant_violation: return "invariant_violation";
        case ExitCode::placement_failure:   return "placement_failure";
    }
    return "unknown";
}

std::string build_ok_envelope(std::string_view message, const nlohmann::json& data)
{
    nlohmann::json j;
    j["status"]  = "ok";
    j["code"]    = "ok";
    j["message"] = std::string(message);
    if (!data.is_null() && !data.empty()) j["data"] = data;
    return j.dump();
}

std::string build_err_envelope(ExitCode code, std::string_view message)
{
    nlohmann::json j;
    j["status"]  = "err";
    j["code"]    = code_name(code);
    j["message"] = std::string(message);
    return j.dump();
}

void print_ok(const GlobalOpts& opts, std::string_view message)
{
    print_ok(opts, message, nlohmann::json::object());
}

void print_ok(const GlobalOpts& opts, std::string_view message, const nlohmann::json& data)
{
    if (opts.json) {
        std::string body = build_ok_envelope(message, data);
        body.push_back('\n');
        std::fputs(body.c_str(), stdout);
    } else {
        std::fputs("ok: ", stdout);
        std::fwrite(message.data(), 1, message.size(), stdout);
        std::fputc('\n', stdout);
    }
    std::fflush(stdout);
}

void print_err(const GlobalOpts& opts, ExitCode code, std::string_view message)
{
    if (opts.json) {
        std::string body = build_err_envelope(code, message);
        body.push_back('\n');
        std::fputs(body.c_str(), stdout);
    } else {
        std::fputs("err: ", stderr);
        std::fputs(code_name(code), stderr);
        std::fputs(": ", stderr);
        std::fwrite(message.data(), 1, message.size(), stderr);
        std::fputc('\n', stderr);
    }
    std::fflush(stdout);
    std::fflush(stderr);
}

} // namespace orca_cli
