#include "invariants.hpp"

#include <libslic3r/miniz_extension.hpp>

namespace orca_cli {

std::vector<ZipEntry> unzip_to_memory(const std::string& zip_path)
{
    using namespace Slic3r;

    mz_zip_archive archive{};
    if (!open_zip_reader(&archive, zip_path))
        throw InvariantViolation("cannot open archive: " + zip_path);

    std::vector<ZipEntry> out;
    const mz_uint n = mz_zip_reader_get_num_files(&archive);
    out.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&archive, i, &st)) continue;
        if (st.m_is_directory)                          continue;

        std::vector<char> bytes(static_cast<size_t>(st.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&archive, i, bytes.data(), bytes.size(), 0))
            continue;
        out.push_back(ZipEntry{ st.m_filename, std::move(bytes) });
    }
    close_zip_reader(&archive);
    return out;
}

// Concrete invariant implementations land in Tasks 1.5 / 1.6 / 1.7.

} // namespace orca_cli
