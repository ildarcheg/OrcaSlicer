// tests/cli/unit/test_project_tab_ops.cpp
#include <catch2/catch_all.hpp>
#include "project_tab_ops.hpp"
#include "project_ops.hpp"
#include "io.hpp"
#include "../test_common.hpp"

#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <boost/filesystem.hpp>

#include <memory>
#include <string>

using namespace orca_cli;

// Helper: construct an empty ProjectState with a single empty plate and
// nothing in model.model_info / model.profile_info. Mirrors the pattern in
// tests/cli/unit/test_project_ops.cpp for plate tests.
static ProjectState make_empty_state() {
    ProjectState s;
    s.project_config = std::make_unique<Slic3r::DynamicPrintConfig>();
    s.model          = std::make_unique<Slic3r::Model>();
    s.plates.push_back(std::make_unique<Slic3r::PlateData>());
    return s;
}

TEST_CASE("orca-cli: info_view on empty model returns six empty strings",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    auto v = info_view(s);
    REQUIRE(v.title.empty());
    REQUIRE(v.description.empty());
    REQUIRE(v.license.empty());
    REQUIRE(v.copyright.empty());
    REQUIRE(v.cover.empty());
    REQUIRE(v.origin.empty());
}

TEST_CASE("orca-cli: info_view returns populated ModelInfo fields verbatim",
          "[orca-cli][project-tab][unit]")
{
    auto s = make_empty_state();
    s.model->model_info = std::make_shared<Slic3r::ModelInfo>();
    s.model->model_info->model_name  = "T";
    s.model->model_info->description = "D";
    s.model->model_info->license     = "MIT";
    s.model->model_info->copyright   = R"([{"author":"Z"}])";
    s.model->model_info->cover_file  = "Auxiliaries/.thumbnails/thumbnail_3mf.png";
    s.model->model_info->origin      = "OrcaSlicer";
    auto v = info_view(s);
    REQUIRE(v.title       == "T");
    REQUIRE(v.description == "D");
    REQUIRE(v.license     == "MIT");
    REQUIRE(v.copyright   == R"([{"author":"Z"}])");
    REQUIRE(v.cover       == "Auxiliaries/.thumbnails/thumbnail_3mf.png");
    REQUIRE(v.origin      == "OrcaSlicer");
}
