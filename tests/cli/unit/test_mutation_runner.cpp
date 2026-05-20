#include <catch2/catch_all.hpp>
#include "commands/mutation_runner.hpp"

using namespace orca_cli;
using namespace orca_cli::commands;

namespace {
struct DummyError : std::runtime_error { using std::runtime_error::runtime_error; };
}

TEST_CASE("MutationExceptionMap dispatches custom exceptions first",
          "[orca-cli][cleanup][T8]") {
    GlobalOpts g; g.json = false;
    MutationExceptionMap em;
    em.on<DummyError>(ExitCode::bad_config);
    int rc = em.handle(g, DummyError("boom"));
    REQUIRE(rc == int(ExitCode::bad_config));
}

TEST_CASE("MutationExceptionMap default mapping for invalid_argument",
          "[orca-cli][cleanup][T8]") {
    GlobalOpts g; g.json = false;
    MutationExceptionMap em;
    em.set_default_invalid_argument(ExitCode::duplicate_name);
    int rc = em.handle(g, std::invalid_argument("dup"));
    REQUIRE(rc == int(ExitCode::duplicate_name));
}
