#include <catch2/catch_all.hpp>
#include "output.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace orca_cli;

TEST_CASE("print_ok JSON envelope is parseable and well-formed", "[orca-cli][cleanup][T9]") {
    nlohmann::json data;
    data["plates"] = nlohmann::json::array();
    data["plates"].push_back({{"index", 1}, {"name", "P\"weird\""}});
    std::string body = build_ok_envelope("listed 1 plates", data);
    auto j = nlohmann::json::parse(body);
    REQUIRE(j["status"]  == "ok");
    REQUIRE(j["code"]    == "ok");
    REQUIRE(j["message"] == "listed 1 plates");
    REQUIRE(j["data"]["plates"][0]["name"] == "P\"weird\"");
}

TEST_CASE("build_err_envelope encodes code name", "[orca-cli][cleanup][T9]") {
    std::string body = build_err_envelope(ExitCode::bad_config, "key x");
    auto j = nlohmann::json::parse(body);
    REQUIRE(j["status"] == "err");
    REQUIRE(j["code"]   == "bad_config");
    REQUIRE(j["message"] == "key x");
}

TEST_CASE("emit_list_response writes JSON envelope", "[orca-cli][cleanup][T10]") {
    using namespace orca_cli;
    struct Row { int idx; std::string name; };
    std::vector<Row> rows = {{1, "First"}, {2, "Second"}};

    // Capture stdout to a temp file so we can verify what emit_list_response writes.
    const char* tmp_path = "t10_stdout.txt";
    std::FILE* saved = std::freopen(tmp_path, "w", stdout);
    REQUIRE(saved != nullptr);

    GlobalOpts g; g.json = true;
    emit_list_response(g, "items", "listed 2 items", rows,
        [](const Row& r) {
            return nlohmann::json{{"index", r.idx}, {"name", r.name}};
        },
        [](const Row& r) { return std::to_string(r.idx) + ": " + r.name; });

    std::fflush(stdout);
    std::freopen("CON", "w", stdout); // Windows: reattach stdout to console

    std::ifstream f(tmp_path);
    std::stringstream ss; ss << f.rdbuf();
    std::string text = ss.str();
    std::remove(tmp_path);

    auto j = nlohmann::json::parse(text);
    REQUIRE(j["status"] == "ok");
    REQUIRE(j["data"]["items"].size() == 2);
    REQUIRE(j["data"]["items"][1]["name"] == "Second");
}
