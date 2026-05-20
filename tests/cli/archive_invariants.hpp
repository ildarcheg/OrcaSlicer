#pragma once
// Archive-level invariant assertions used by orca-cli e2e tests. These are
// stricter than the runtime invariants in src/cli/invariants.hpp: they
// inspect the actual byte-level structure of the produced .3mf (PNG IHDR
// dimensions, point counts inside printable_area, the presence of specific
// metadata keys) rather than just relationships and roundtrip equivalence.
//
// Every helper uses Catch2 REQUIREs internally, so they must be called from
// within a TEST_CASE.

#include <boost/filesystem.hpp>
#include <string>

namespace orca_cli_test::archive {

namespace fs = boost::filesystem;

// Every Target="..." in any *.rels entry points to a real archive entry.
// Equivalent to the runtime verify_relationships guard but Catch2-flavoured.
void assert_relationships_resolve(const fs::path& zip);

// Every Metadata/plate_<N>.png and Metadata/plate_<N>_small.png that exists
// in the archive must be exactly 128x128 pixels (parsed from the PNG IHDR
// chunk). The GUI's plate panel hard-codes this size.
void assert_plate_thumbnails_128(const fs::path& zip);

// Metadata/project_settings.config -> "printable_area" array must contain
// exactly 4 entries (the 4 corners of the bed).
void assert_printable_area_4_points(const fs::path& zip);

// Every <part ...> element under Metadata/model_settings.config must have
// a <metadata key="source_file" value="..."/> child. Without it the GUI
// rejects the part on open in some Orca versions.
void assert_parts_have_source_file(const fs::path& zip);

// The first <object> in Metadata/model_settings.config matching
// object_name (via <metadata key="name" value="..."/>) must have a
// <metadata key="extruder" value="N"/> equal to expected_extruder.
void assert_object_extruder(const fs::path&    zip,
                            const std::string& object_name,
                            int                expected_extruder);

// In Metadata/model_settings.config: the <part ...> child of the <object>
// matching `object_name` whose <metadata key="name" value="..."/> matches
// `part_name` must carry a <metadata key="extruder" value="N"/> equal to
// `expected_extruder`. Bug C class lock-in for per-volume filament
// assignment: catches regressions where set-filament --part is silently
// ignored by the serializer.
void assert_part_extruder(const fs::path&    zip,
                          const std::string& object_name,
                          const std::string& part_name,
                          int                expected_extruder);

// Run the small set of "every e2e archive must pass these" checks. Right
// now: relationships + 128px thumbnails. printable_area is also a
// well-formed-archive check but lives outside this helper because it is
// the most expensive check (re-parses the JSON config) and a few e2e
// scenarios test states where the config is intentionally mutated.
void run_all_basic(const fs::path& zip);

} // namespace orca_cli_test::archive
