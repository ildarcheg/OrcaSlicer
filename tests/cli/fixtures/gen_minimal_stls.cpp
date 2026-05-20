// Generates the committed Layer A fixture two_cubes.stl: a binary STL
// containing two disjoint 10 mm cubes (at the origin and offset by
// (30,0,0)). Used by the orca-cli split-to-parts tests as a deterministic
// 2-component fixture. Idempotent: regenerates the same bytes every run.
//
// Build target: gen_minimal_stls. Run manually after a clean build to
// regenerate the .stl when this generator changes. The .stl itself is
// committed to git so CI does not need to run the generator.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct Vec3 { float x, y, z; };

// Emit a binary STL header + facet list to `path`.
void write_binary_stl(const std::string& path, const std::vector<std::array<Vec3, 4>>& facets) {
    std::ofstream f(path, std::ios::binary);
    char header[80] = {};
    std::strncpy(header, "orca-cli two_cubes fixture", sizeof(header) - 1);
    f.write(header, 80);
    uint32_t count = static_cast<uint32_t>(facets.size());
    f.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& tri : facets) {
        // tri[0] is the face normal, tri[1..3] are the three vertices.
        for (int i = 0; i < 4; ++i) {
            f.write(reinterpret_cast<const char*>(&tri[i]), sizeof(Vec3));
        }
        uint16_t attr = 0;
        f.write(reinterpret_cast<const char*>(&attr), sizeof(attr));
    }
}

// Append the 12 triangles of an axis-aligned cube spanning min..max.
void append_cube(std::vector<std::array<Vec3, 4>>& out, Vec3 mn, Vec3 mx) {
    auto tri = [&](Vec3 n, Vec3 a, Vec3 b, Vec3 c) {
        out.push_back({n, a, b, c});
    };
    // -X face
    tri({-1,0,0}, {mn.x,mn.y,mn.z}, {mn.x,mx.y,mn.z}, {mn.x,mx.y,mx.z});
    tri({-1,0,0}, {mn.x,mn.y,mn.z}, {mn.x,mx.y,mx.z}, {mn.x,mn.y,mx.z});
    // +X face
    tri({ 1,0,0}, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mx.z}, {mx.x,mx.y,mn.z});
    tri({ 1,0,0}, {mx.x,mn.y,mn.z}, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z});
    // -Y face
    tri({0,-1,0}, {mn.x,mn.y,mn.z}, {mn.x,mn.y,mx.z}, {mx.x,mn.y,mx.z});
    tri({0,-1,0}, {mn.x,mn.y,mn.z}, {mx.x,mn.y,mx.z}, {mx.x,mn.y,mn.z});
    // +Y face
    tri({0, 1,0}, {mn.x,mx.y,mn.z}, {mx.x,mx.y,mn.z}, {mx.x,mx.y,mx.z});
    tri({0, 1,0}, {mn.x,mx.y,mn.z}, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z});
    // -Z face
    tri({0,0,-1}, {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z});
    tri({0,0,-1}, {mn.x,mn.y,mn.z}, {mx.x,mx.y,mn.z}, {mn.x,mx.y,mn.z});
    // +Z face
    tri({0,0, 1}, {mn.x,mn.y,mx.z}, {mn.x,mx.y,mx.z}, {mx.x,mx.y,mx.z});
    tri({0,0, 1}, {mn.x,mn.y,mx.z}, {mx.x,mx.y,mx.z}, {mx.x,mn.y,mx.z});
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: gen_minimal_stls <out-dir>\n");
        return 1;
    }
    const std::string out_dir = argv[1];
    std::vector<std::array<Vec3, 4>> facets;
    facets.reserve(24);
    append_cube(facets, {  0, 0, 0}, { 10, 10, 10});
    append_cube(facets, { 30, 0, 0}, { 40, 10, 10});
    write_binary_stl(out_dir + "/two_cubes.stl", facets);
    return 0;
}
