cmake_minimum_required(VERSION 3.13)

# Test-fixture locations. Defaults point in-tree under tests/cli/fixtures/
# so a fresh clone builds + tests cleanly on any machine. Override via
# -DORCA_CLI_REF_3MF=... / -DORCA_CLI_STL_DIR=... at configure time if
# you want to point at out-of-tree fixtures (e.g. shared workstream dir).
set(ORCA_CLI_REF_3MF
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/temp_project_for_orca_slicer.3mf"
    CACHE FILEPATH "Reference 3mf for orca-cli tests; tests SUCCEED+skip if missing")
set(ORCA_CLI_STL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    CACHE PATH "STL fixture directory for orca-cli tests")

# Layer A fixture dir (committed in-tree, always present). Used by the
# generator + a small number of tests that load STLs by exact path
# (vs. via ORCA_CLI_STL_DIR which is also in-tree now but conceptually
# separate -- a future contributor could repoint STL_DIR to an external
# corpus while leaving the Layer A fixtures stable).
set(ORCA_CLI_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    CACHE PATH "Committed in-tree fixture directory for orca-cli tests")

# Generator for the two_cubes.stl Layer A fixture. Build target; not
# wired into the test pipeline. Run manually when regenerating the .stl.
add_executable(gen_minimal_stls fixtures/gen_minimal_stls.cpp)

file(GLOB ORCA_CLI_TEST_SRC CONFIGURE_DEPENDS
    test_main.cpp
    test_common.cpp
    archive_invariants.cpp
    unit/*.cpp
    roundtrip/*.cpp
    e2e/*.cpp)

add_executable(cli_tests ${ORCA_CLI_TEST_SRC})

target_compile_definitions(cli_tests PRIVATE
    ORCA_CLI_REF_3MF="${ORCA_CLI_REF_3MF}"
    ORCA_CLI_STL_DIR="${ORCA_CLI_STL_DIR}"
    ORCA_CLI_FIXTURES_DIR="${ORCA_CLI_FIXTURES_DIR}"
    ORCA_CLI_EXE="$<TARGET_FILE:orca-cli>"
)

target_include_directories(cli_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/cli
)

target_link_libraries(cli_tests PRIVATE
    Catch2::Catch2WithMain
    test_common
    orca_cli_core
    libslic3r
    Boost::filesystem
    Boost::system
    nlohmann_json
)

add_dependencies(cli_tests orca-cli)

orcaslicer_copy_test_dlls()

include(Catch)
catch_discover_tests(cli_tests
    TEST_PREFIX "orca-cli: "
    EXTRA_ARGS  --order rand --warn NoAssertions)
