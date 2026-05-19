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

void remove_plate(ProjectState& s, const std::string& name)
{
    if (s.plates.size() <= 1)
        throw std::invalid_argument("cannot remove the only plate");

    auto it = std::find_if(
        s.plates.begin(), s.plates.end(),
        [&](const std::unique_ptr<Slic3r::PlateData>& p) {
            return p->plate_name == name;
        });
    if (it == s.plates.end())
        throw std::out_of_range("plate not found: " + name);

    s.plates.erase(it);
    // Re-index the remaining plates so plate_index stays contiguous from 0;
    // store_bbs_3mf and the GUI both treat plate_index as the canonical
    // 1-based position on disk (plate_N.png follows from plate_index + 1).
    for (size_t i = 0; i < s.plates.size(); ++i)
        s.plates[i]->plate_index = static_cast<int>(i);
}

void rename_plate(ProjectState& s, const std::string& from, const std::string& to)
{
    // Existence check first: `--from missing --to missing` must report the
    // missing-`from` error rather than silently no-op.
    Slic3r::PlateData* found = nullptr;
    for (auto& p : s.plates) {
        if (p->plate_name == from) found = p.get();
    }
    if (!found)
        throw std::out_of_range("plate not found: " + from);

    // Self-rename is a no-op AFTER existence is confirmed.
    if (from == to) return;

    // Duplicate-`to` check (excluding the plate we'd be renaming, though for
    // distinct from/to here that distinction is moot -- p->plate_name == to
    // cannot also equal from).
    for (auto& p : s.plates) {
        if (p.get() != found && p->plate_name == to)
            throw std::invalid_argument("duplicate plate name: " + to);
    }
    found->plate_name = to;
}

} // namespace orca_cli
