cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(orca-cli
    main.cpp
    globals.cpp
    output.cpp
)

target_include_directories(orca-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cli11
)

target_link_libraries(orca-cli PRIVATE
    libslic3r
    Boost::filesystem
    Boost::system
    Boost::thread
)

if (WIN32)
    target_compile_definitions(orca-cli PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

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
