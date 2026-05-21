// src/cli/project_tab_ops.cpp
#include "project_tab_ops.hpp"
#include <stdexcept>

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
const std::vector<std::string>& allowed_info_fields()                { throw std::logic_error("not implemented"); }
void info_clear(ProjectState&, const std::vector<std::string>&)      { throw std::logic_error("not implemented"); }

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

bool is_png(const boost::filesystem::path&)                          { throw std::logic_error("not implemented"); }
void embed_cover_image(ProjectState&, const boost::filesystem::path&,
                       CoverTarget)                                  { throw std::logic_error("not implemented"); }
void clear_cover_image(ProjectState&, CoverTarget)                   { throw std::logic_error("not implemented"); }

std::string sanitize_aux_name(const std::string&)                    { throw std::logic_error("not implemented"); }

} // namespace orca_cli
