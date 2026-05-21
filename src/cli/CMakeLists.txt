cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core orca-cli library. Everything except the main entry point lives here so
# the cli_tests target can link the same translation units the binary uses.
add_library(orca_cli_core STATIC
    globals.cpp
    output.cpp
    io.cpp
    project_ops.cpp
    project_tab_ops.cpp
    invariants.cpp
    nanosvg_impl.cpp
    png_placeholder.cpp
    placement.cpp
    commands/project_init.cpp
    commands/plate.cpp
    commands/object.cpp
    commands/object_parse_vec3.cpp
    commands/config.cpp
    commands/inspect.cpp
)

target_include_directories(orca_cli_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cli11
)

target_link_libraries(orca_cli_core PUBLIC
    libslic3r
    Boost::filesystem
    Boost::system
    Boost::thread
    nlohmann_json
)

if (WIN32)
    target_compile_definitions(orca_cli_core PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(orca-cli
    main.cpp
)

target_link_libraries(orca-cli PRIVATE
    orca_cli_core
)

# Copy DLL deps next to orca-cli so it can be invoked directly from the build dir.
# Use copy_directory_if_different (CMake 3.26+) so incremental builds only re-copy
# changed files instead of the entire OrcaSlicer DLL set every time (~hundreds of MB).
if (WIN32 AND TARGET OrcaSlicer)
    add_custom_command(TARGET orca-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "$<TARGET_FILE_DIR:OrcaSlicer>"
                "$<TARGET_FILE_DIR:orca-cli>"
        COMMENT "Mirroring OrcaSlicer DLL dir (changed files only) next to orca-cli for runtime")
endif()
