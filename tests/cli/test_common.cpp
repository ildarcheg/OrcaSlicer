#include "test_common.hpp"
#include <boost/process.hpp>
#include <boost/process/io.hpp>
#include <chrono>
#include <random>

namespace bp = boost::process;
namespace orca_cli_test {

fs::path ref_3mf() {
    fs::path p(ORCA_CLI_REF_3MF);
    return fs::exists(p) ? p : fs::path{};
}

fs::path stl_dir() { return fs::path(ORCA_CLI_STL_DIR); }
fs::path cli_exe() { return fs::path(ORCA_CLI_EXE); }

fs::path make_temp_dir() {
    using namespace std::chrono;
    auto ts = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    std::mt19937_64 rng{std::random_device{}()};
    fs::path p = fs::temp_directory_path()
               / ("orca-cli-test-" + std::to_string(ts) + "-" + std::to_string(rng()));
    fs::create_directories(p);
    return p;
}

fs::path copy_ref_to_temp(const fs::path& temp_dir, const std::string& tag) {
    fs::path dst = temp_dir / ("orca-cli-work-" + tag + ".3mf");
    fs::copy_file(ref_3mf(), dst, fs::copy_options::overwrite_existing);
    return dst;
}

RunResult run_cli(const std::vector<std::string>& args) {
    bp::ipstream out, err;
    bp::child c(cli_exe().string(), bp::args(args), bp::std_out > out, bp::std_err > err);
    std::string so((std::istreambuf_iterator<char>(out)), {});
    std::string se((std::istreambuf_iterator<char>(err)), {});
    c.wait();
    return RunResult{ c.exit_code(), so, se };
}

} // namespace orca_cli_test
