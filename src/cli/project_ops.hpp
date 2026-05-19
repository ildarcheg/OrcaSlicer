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

    // View suitable for passing to libslic3r APIs that take PlateDataPtrs.
    Slic3r::PlateDataPtrs plate_data_ptrs() const {
        Slic3r::PlateDataPtrs r;
        r.reserve(plates.size());
        for (auto& p : plates) r.push_back(p.get());
        return r;
    }
};

} // namespace orca_cli
