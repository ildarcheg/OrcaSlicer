#pragma once
#include <string>
#include <vector>

namespace orca_cli {

// Returns a 128x128 RGBA PNG with every pixel = 0xC0 (gray).
// Chosen over fully transparent because some renderer paths handle gray more
// reliably than alpha=0 (matches the bambu-cli sister project's v1.2 finding).
//
// Used by save_project's passthrough_missing_thumbnails to inject placeholder
// thumbnail bytes for plates that don't have a source-archive PNG to copy
// from (i.e. plates created by `plate add` in P2+).
std::vector<char> make_placeholder_png_128_gray_C0();

struct PlateThumbnailPaths {
    std::string mid;       // Metadata/plate_<n>.png
    std::string small;     // Metadata/plate_<n>_small.png
    std::string no_light;  // Metadata/plate_no_light_<n>.png
    std::string top;       // Metadata/top_<n>.png
    std::string pick;      // Metadata/pick_<n>.png
};

// Returns the canonical plate-thumbnail entry names for plate index
// `n_one_based` (1-based, matching on-disk PNG naming).
PlateThumbnailPaths plate_thumbnail_paths(int n_one_based);

} // namespace orca_cli
