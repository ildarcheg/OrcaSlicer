// src/cli/project_tab_ops.cpp
#include "project_tab_ops.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>

#include <boost/system/error_code.hpp>

namespace orca_cli {

const char* folder_flag(AuxFolder f) {
    switch (f) {
        case AuxFolder::pictures:       return "pictures";
        case AuxFolder::bom:            return "bom";
        case AuxFolder::assembly_guide: return "assembly-guide";
        case AuxFolder::others:         return "others";
    }
    throw std::logic_error("unreachable: AuxFolder out of range");
}

const char* folder_json_key(AuxFolder f) {
    switch (f) {
        case AuxFolder::pictures:       return "pictures";
        case AuxFolder::bom:            return "bom";
        case AuxFolder::assembly_guide: return "assembly_guide";
        case AuxFolder::others:         return "others";
    }
    throw std::logic_error("unreachable: AuxFolder out of range");
}

const char* folder_subdir(AuxFolder f) {
    switch (f) {
        case AuxFolder::pictures:       return "Model Pictures";
        case AuxFolder::bom:            return "Bill of Materials";
        case AuxFolder::assembly_guide: return "Assembly Guide";
        case AuxFolder::others:         return "Others";
    }
    throw std::logic_error("unreachable: AuxFolder out of range");
}

InfoView info_view(const ProjectState& s) {
    InfoView v;
    if (!s.model || !s.model->model_info) return v;  // all six fields empty
    const auto& mi = *s.model->model_info;
    v.title       = mi.model_name;
    v.description = mi.description;
    v.license     = mi.license;
    v.copyright   = mi.copyright;
    v.cover       = mi.cover_file;
    v.origin      = mi.origin;
    return v;
}
bool any_field_set(const InfoSetParams& p) {
    return p.title.has_value()
        || p.description.has_value()
        || p.license.has_value()
        || p.copyright.has_value()
        || p.cover.has_value();
}

void info_set(ProjectState& s, const InfoSetParams& p) {
    if (!s.model->model_info)
        s.model->model_info = std::make_shared<Slic3r::ModelInfo>();
    auto& mi = *s.model->model_info;
    if (p.title)       mi.model_name  = *p.title;
    if (p.description) mi.description = *p.description;
    if (p.license)     mi.license     = *p.license;
    if (p.copyright)   mi.copyright   = *p.copyright;
    if (p.cover)       embed_cover_image(s, *p.cover, CoverTarget::Info);
}
const std::vector<std::string>& allowed_info_fields() {
    static const std::vector<std::string> v{
        "title", "description", "license", "copyright", "cover"
    };
    return v;
}

void info_clear(ProjectState& s, const std::vector<std::string>& fields) {
    const auto& allowed = allowed_info_fields();
    for (const auto& f : fields) {
        if (std::find(allowed.begin(), allowed.end(), f) == allowed.end()) {
            std::string msg = "unknown field: '" + f + "'. Allowed:";
            for (const auto& a : allowed) msg += " " + a;
            throw InvalidField(msg);
        }
    }
    if (!s.model->model_info) {
        // Nothing to clear on string fields. cover-clear may still need to
        // run if the canonical embedded image exists (handled by
        // clear_cover_image, which is a no-op on a missing file).
        for (const auto& f : fields)
            if (f == "cover") clear_cover_image(s, CoverTarget::Info);
        return;
    }
    auto& mi = *s.model->model_info;
    for (const auto& f : fields) {
        if      (f == "title")       mi.model_name.clear();
        else if (f == "description") mi.description.clear();
        else if (f == "license")     mi.license.clear();
        else if (f == "copyright")   mi.copyright.clear();
        else if (f == "cover")       clear_cover_image(s, CoverTarget::Info);
    }
}

ProfileView profile_view(const ProjectState&)                        { throw std::logic_error("not implemented"); }
bool any_field_set(const ProfileSetParams&)                          { throw std::logic_error("not implemented"); }
void profile_set(ProjectState&, const ProfileSetParams&)             { throw std::logic_error("not implemented"); }
const std::vector<std::string>& allowed_profile_fields()             { throw std::logic_error("not implemented"); }
void profile_clear(ProjectState&, const std::vector<std::string>&)   { throw std::logic_error("not implemented"); }

std::vector<AuxEntry> aux_list(const ProjectState&)                  { throw std::logic_error("not implemented"); }
void aux_add(ProjectState&, const AuxAddParams&)                     { throw std::logic_error("not implemented"); }
void aux_remove(ProjectState&, AuxFolder, const std::string&)        { throw std::logic_error("not implemented"); }
void aux_export(const ProjectState&, AuxFolder, const std::string&,
                const boost::filesystem::path&)                      { throw std::logic_error("not implemented"); }

bool is_png(const boost::filesystem::path& p) {
    boost::system::error_code ec;
    if (!boost::filesystem::is_regular_file(p, ec)) return false;
    std::ifstream f(p.string(), std::ios::binary);
    if (!f) return false;
    unsigned char buf[8] = {};
    f.read(reinterpret_cast<char*>(buf), 8);
    if (f.gcount() != 8) return false;
    static constexpr unsigned char kPngSig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    for (int i = 0; i < 8; ++i)
        if (buf[i] != kPngSig[i]) return false;
    return true;
}
void embed_cover_image(ProjectState&, const boost::filesystem::path&,
                       CoverTarget)                                  { throw std::logic_error("not implemented"); }
void clear_cover_image(ProjectState&, CoverTarget)                   { throw std::logic_error("not implemented"); }

std::string sanitize_aux_name(const std::string&)                    { throw std::logic_error("not implemented"); }

} // namespace orca_cli
