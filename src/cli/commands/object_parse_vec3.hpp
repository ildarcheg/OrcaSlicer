#pragma once
#include <libslic3r/Point.hpp>
#include <optional>
#include <string>

namespace orca_cli::commands {

struct ParsedVec3 {
    Slic3r::Vec3d values;          // broadcast/extended to 3 components
    int           component_count; // 1, 2, or 3 -- the original input arity
};

// Parse "s" / "x,y" / "x,y,z". 1-component -> {s,s,s}; 2-component -> {x,y,0}.
// Returns nullopt if any token is empty, non-finite, or not parseable;
// also nullopt for >3 components.
std::optional<ParsedVec3> parse_vec3(const std::string& s);

} // namespace orca_cli::commands
