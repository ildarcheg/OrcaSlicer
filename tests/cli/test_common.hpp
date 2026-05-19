#pragma once
#include <boost/filesystem.hpp>
#include <string>
#include <vector>

namespace orca_cli_test {

namespace fs = boost::filesystem;

fs::path ref_3mf();
fs::path stl_dir();
fs::path cli_exe();
fs::path make_temp_dir();
fs::path copy_ref_to_temp(const fs::path& temp_dir, const std::string& tag);

struct RunResult { int exit_code; std::string stdout_; std::string stderr_; };
RunResult run_cli(const std::vector<std::string>& args);

} // namespace orca_cli_test
