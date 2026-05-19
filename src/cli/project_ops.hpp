#pragma once
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/Format/bbs_3mf.hpp>
#include <memory>
#include <vector>

namespace orca_cli {

// In-memory representation of a loaded 3mf project.
// Owns the Model, the project DynamicPrintConfig, and the PlateData vector
// (which describes plate <-> object-instance mappings).
struct ProjectState {
    std::unique_ptr<Slic3r::Model>                  model;
    std::unique_ptr<Slic3r::DynamicPrintConfig>     project_config;
    std::vector<std::unique_ptr<Slic3r::PlateData>> plates;

    // Path to the .3mf that load_project() read this state from. Empty if
    // the state was constructed from scratch. save_project() uses this to
    // copy through binary blobs that the libslic3r store path emits a
    // relationship for but does not re-encode (notably plate thumbnails:
    // load_bbs_3mf reads them as raw PNG bytes into PlateData::plate_thumbnail
    // .pixels, but store_bbs_3mf only writes thumbnails when the caller
    // provides decoded RGBA via StoreParams::thumbnail_data). For
    // clone-and-mutate flows like `project init` we just want to pass these
    // blobs through verbatim so the resulting archive's relationships file
    // does not dangle. See save_project() in src/cli/io.cpp.
    std::string source_path;

    // View suitable for passing to libslic3r APIs that take PlateDataPtrs.
    Slic3r::PlateDataPtrs plate_data_ptrs() const {
        Slic3r::PlateDataPtrs r;
        r.reserve(plates.size());
        for (auto& p : plates) r.push_back(p.get());
        return r;
    }
};

} // namespace orca_cli
