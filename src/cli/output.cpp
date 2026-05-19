#include "output.hpp"
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

static std::string escape_json(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 2);
    for (char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(ch));
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

void print_ok(const GlobalOpts& opts, std::string_view message, std::string_view data_json)
{
    if (opts.json) {
        std::string body = "{\"status\":\"ok\",\"code\":\"ok\",\"message\":\"" + escape_json(message) + "\"";
        if (!data_json.empty())
            body += ",\"data\":{" + std::string(data_json) + "}";
        body += "}\n";
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
        std::string body = std::string("{\"status\":\"err\",\"code\":\"") + code_name(code)
                         + "\",\"message\":\"" + escape_json(message) + "\"}\n";
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
