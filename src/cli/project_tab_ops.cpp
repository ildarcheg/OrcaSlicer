// src/cli/project_tab_ops.cpp
#include "project_tab_ops.hpp"
#include <stdexcept>

namespace orca_cli {

const char* folder_flag(AuxFolder)       { throw std::logic_error("not implemented"); }
const char* folder_json_key(AuxFolder)   { throw std::logic_error("not implemented"); }
const char* folder_subdir(AuxFolder)     { throw std::logic_error("not implemented"); }

InfoView info_view(const ProjectState&)                              { throw std::logic_error("not implemented"); }
bool any_field_set(const InfoSetParams&)                             { throw std::logic_error("not implemented"); }
void info_set(ProjectState&, const InfoSetParams&)                   { throw std::logic_error("not implemented"); }
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
