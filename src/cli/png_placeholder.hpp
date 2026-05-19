#pragma once
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

} // namespace orca_cli
