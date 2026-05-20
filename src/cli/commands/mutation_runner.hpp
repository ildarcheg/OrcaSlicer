#pragma once
#include "../globals.hpp"
#include "../output.hpp"
#include "../invariants.hpp"
#include "../io.hpp"
#include "../project_ops.hpp"
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace orca_cli::commands {

// Registry of exception-type -> ExitCode handlers used by run_mutation.
// Handlers are checked in registration order via dynamic_cast. After all
// custom handlers are tried, std::invalid_argument and std::out_of_range
// fall through to their configurable defaults (duplicate_name and
// unknown_reference respectively). Anything else maps to parse_failure.
class MutationExceptionMap {
public:
    // Register a handler for exception type T. Handlers are tried in
    // registration order; the first matching cast wins.
    template <typename T>
    MutationExceptionMap& on(ExitCode code) {
        m_handlers.push_back({
            [](const std::exception& e) { return dynamic_cast<const T*>(&e) != nullptr; },
            code,
        });
        return *this;
    }

    MutationExceptionMap& set_default_invalid_argument(ExitCode c) { m_invalid_argument = c; return *this; }
    MutationExceptionMap& set_default_out_of_range(ExitCode c)     { m_out_of_range = c;     return *this; }

    int handle(const GlobalOpts& g, const std::exception& e) const {
        for (const auto& h : m_handlers) {
            if (h.predicate(e)) {
                print_err(g, h.code, e.what());
                return int(h.code);
            }
        }
        if (dynamic_cast<const std::invalid_argument*>(&e)) {
            print_err(g, m_invalid_argument, e.what());
            return int(m_invalid_argument);
        }
        if (dynamic_cast<const std::out_of_range*>(&e)) {
            print_err(g, m_out_of_range, e.what());
            return int(m_out_of_range);
        }
        print_err(g, ExitCode::parse_failure, e.what());
        return int(ExitCode::parse_failure);
    }

private:
    struct Handler {
        std::function<bool(const std::exception&)> predicate;
        ExitCode                                   code;
    };
    std::vector<Handler> m_handlers;
    ExitCode             m_invalid_argument = ExitCode::duplicate_name;
    ExitCode             m_out_of_range     = ExitCode::unknown_reference;
};

// Run a load-mutate-save envelope with standardized exception mapping.
//
//   1. Verifies `input` exists (returns early on file_not_found).
//   2. Resolves the save target (input or --output).
//   3. Calls mutate(state) inside a try block:
//        - InvariantViolation -> ExitCode::invariant_violation (always)
//        - std::exception     -> delegated to `em`
//   4. On success, calls print_ok(g, ok_message) and returns ok.
template <typename Mutator>
int run_mutation(const GlobalOpts&           g,
                 const std::string&          input,
                 const std::string&          ok_message,
                 const MutationExceptionMap& em,
                 Mutator&&                   mutate)
{
    if (int rc = check_input_exists(g, input); rc != int(ExitCode::ok))
        return rc;

    const std::string out = resolve_save_target(g, input);
    try {
        auto state = load_project(input);
        mutate(state);
        save_project(state, out);
    } catch (const InvariantViolation& e) {
        print_err(g, ExitCode::invariant_violation, e.what());
        return int(ExitCode::invariant_violation);
    } catch (const std::exception& e) {
        return em.handle(g, e);
    }

    print_ok(g, ok_message);
    return int(ExitCode::ok);
}

} // namespace orca_cli::commands
