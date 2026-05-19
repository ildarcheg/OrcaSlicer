cmake_minimum_required(VERSION 3.13)

set(ORCA_CLI_REF_3MF
    "C:/Users/ildarcheg/Documents/GitHub/slicer_tamplates/temp_project_for_orca_slicer.3mf"
    CACHE FILEPATH "Reference 3mf for orca-cli tests; tests SUCCEED+skip if missing")
set(ORCA_CLI_STL_DIR
    "C:/Users/ildarcheg/Documents/GitHub/slicer_tamplates"
    CACHE PATH "STL fixture directory for orca-cli tests")

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
)

add_dependencies(cli_tests orca-cli)

orcaslicer_copy_test_dlls()

include(Catch)
catch_discover_tests(cli_tests
    TEST_PREFIX "orca-cli: "
    EXTRA_ARGS  --order rand --warn NoAssertions)
