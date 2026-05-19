// project_ops.cpp -- mutations on a loaded ProjectState. Phase 2 introduces
// the plate ops; later phases will add object add/transform/filament/config.
#include "project_ops.hpp"

#include <algorithm>
#include <stdexcept>

namespace orca_cli {

void add_plate(ProjectState& s, const std::string& name)
{
    for (auto& p : s.plates) {
        if (p->plate_name == name)
            throw std::invalid_argument("duplicate plate name: " + name);
    }

    auto pd = std::make_unique<Slic3r::PlateData>();
    pd->plate_name  = name;
    pd->plate_index = static_cast<int>(s.plates.size());
    // No thumbnail_file or thumbnail_data set here. passthrough_missing_thumbnails
    // in save_project injects placeholder PNG bytes for any plate whose
    // Metadata/plate_N.png and plate_N_small.png entries are missing from both
    // the source archive and the just-stored .tmp archive.
    s.plates.emplace_back(std::move(pd));
}

} // namespace orca_cli
