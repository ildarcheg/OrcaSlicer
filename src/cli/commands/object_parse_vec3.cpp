#include "object_parse_vec3.hpp"
#include <cmath>
#include <vector>

namespace orca_cli::commands {

std::optional<ParsedVec3> parse_vec3(const std::string& s) {
    if (s.empty()) return std::nullopt;
    std::vector<double> nums;
    std::string token;
    auto trim = [](std::string& t) {
        size_t a = t.find_first_not_of(" \t");
        size_t b = t.find_last_not_of(" \t");
        if (a == std::string::npos) t.clear();
        else t = t.substr(a, b - a + 1);
    };
    auto flush = [&]() -> bool {
        trim(token);
        if (token.empty()) return false;
        try {
            size_t consumed = 0;
            double v = std::stod(token, &consumed);
            if (consumed != token.size()) return false;
            if (!std::isfinite(v))        return false;
            nums.push_back(v);
        } catch (...) { return false; }
        token.clear();
        return true;
    };
    for (char ch : s) {
        if (ch == ',') { if (!flush()) return std::nullopt; }
        else           { token.push_back(ch); }
    }
    if (!flush()) return std::nullopt;
    if (nums.size() == 1) return ParsedVec3{Slic3r::Vec3d(nums[0], nums[0], nums[0]), 1};
    if (nums.size() == 2) return ParsedVec3{Slic3r::Vec3d(nums[0], nums[1], 0.0),     2};
    if (nums.size() == 3) return ParsedVec3{Slic3r::Vec3d(nums[0], nums[1], nums[2]), 3};
    return std::nullopt;
}

} // namespace orca_cli::commands
