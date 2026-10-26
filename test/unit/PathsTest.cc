// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Paths.hh"

#include "Arxiv/Config.hh"

#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>

using Arxiv::Paths;
namespace fs = std::filesystem;

namespace {

// A fake environment so tests never depend on the developer's XDG setup.
struct FakeEnv {
    std::map<std::string, std::string> vars;
    const char* operator()(const char* name) const {
        auto it = vars.find(name);
        return it == vars.end() ? nullptr : it->second.c_str();
    }
};

} // namespace

TEST_CASE("Paths regression: an unwritable install prefix falls back to the XDG defaults",
          "[paths][regression]") {
    // A default build bakes in /usr/local/share and /usr/local/var, which a
    // normal user cannot write; startup used to throw creating them.
    FakeEnv env{{{"HOME", "/home/alice"}}};
    auto unusable = [](const fs::path&) { return false; };

    Paths p = Paths::Resolve(env, "/usr/local/share", "/usr/local/var", unusable);

    SECTION("data") { REQUIRE(p.data_dir == fs::path("/home/alice/.local/share/arxiv-tui")); }
    SECTION("state") { REQUIRE(p.state_dir == fs::path("/home/alice/.local/state/arxiv-tui")); }
}

TEST_CASE("Paths::ApplyTo fills in the runtime files of a config", "[paths]") {
    Paths p;
    p.config_file = "/cfg/arxiv-tui/config.yml";
    p.data_dir = "/data/arxiv-tui";
    p.state_dir = "/state/arxiv-tui";
    Arxiv::Config config;

    SECTION("unset paths default under the data directory") {
        p.ApplyTo(config);
        REQUIRE(config.get_db_file() == "/data/arxiv-tui/articles.db");
        REQUIRE(config.get_ranker_file() == "/data/arxiv-tui/ranker.bin");
        REQUIRE(config.get_download_dir() == "/data/arxiv-tui/downloads");
        REQUIRE(config.get_export_dir() == "/data/arxiv-tui/exports");
        REQUIRE(config.get_keywords_file() == "/data/arxiv-tui/keywords.txt");
    }
    SECTION("paths set in config.yml are kept") {
        config.set_download_dir("/papers");
        config.set_export_dir("/exports");
        config.set_keywords_file("/shared/keywords.txt");
        p.ApplyTo(config);
        REQUIRE(config.get_download_dir() == "/papers");
        REQUIRE(config.get_export_dir() == "/exports");
        REQUIRE(config.get_keywords_file() == "/shared/keywords.txt");
    }
}
