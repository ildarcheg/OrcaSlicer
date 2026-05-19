// Sole translation unit that defines NANOSVG_IMPLEMENTATION /
// NANOSVGRAST_IMPLEMENTATION for the orca-cli binary (and the cli_tests target
// via the orca_cli_core static library). libslic3r references nanosvg's
// nsvgParseFromFile / nsvgParse / nsvgDelete but does not provide the
// implementation -- mirrors what tests/libslic3r/libslic3r_tests_main.cpp and
// src/slic3r/GUI/BitmapCache.cpp do.

// Squelch nanosvg's own minor warnings on MSVC.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4456 4457 4458 4459)
#endif

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
