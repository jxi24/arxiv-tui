// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Config.hh"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

using Arxiv::Config;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// RAII wrapper: creates a uniquely-named temp file, deletes on scope exit.
// Uses PID + atomic counter so parallel ctest -jN runs never share a path.
struct TempConfig {
    std::string path;
    TempConfig() {
        static std::atomic<int> counter{0};
        path = (std::filesystem::temp_directory_path() /
                ("arxiv_test_config_" + std::to_string(::getpid()) + "_" +
                 std::to_string(counter++) + ".yml"))
                   .string();
    }
    ~TempConfig() { std::filesystem::remove(path); }
};

} // namespace

// ---------------------------------------------------------------------------
// Config round-trip: auto_refresh_minutes
// ---------------------------------------------------------------------------

TEST_CASE("Config: auto_refresh_minutes is saved and reloaded", "[config]") {
    TempConfig tmp;

    Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    cfg.set_auto_refresh_minutes(30);
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_auto_refresh_minutes() == 30);
}

TEST_CASE("Config: auto_refresh_minutes defaults to 0 when absent from file", "[config]") {
    TempConfig tmp;

    // Write a minimal config without auto_refresh_minutes
    {
        std::ofstream f(tmp.path);
        f << "article_settings:\n"
          << "  download_dir: /tmp\n"
          << "  topics: [cs.AI]\n";
    }

    Config loaded(tmp.path);
    REQUIRE(loaded.get_auto_refresh_minutes() == 0);
}

// ---------------------------------------------------------------------------
// Config round-trip: recommend_threshold and retrain_interval
// ---------------------------------------------------------------------------

TEST_CASE("Config: recommend_threshold round-trips through save/load", "[config]") {
    TempConfig tmp;

    Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    cfg.set_recommend_threshold(4.2f);
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE_THAT(loaded.get_recommend_threshold(), Catch::Matchers::WithinRel(4.2f, 0.001f));
}

TEST_CASE("Config: retrain_interval round-trips through save/load", "[config]") {
    TempConfig tmp;

    Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    cfg.set_retrain_interval(10);
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_retrain_interval() == 10);
}

// ---------------------------------------------------------------------------
// Config round-trip: obsidian_vault
// ---------------------------------------------------------------------------

TEST_CASE("Config: obsidian_vault round-trips through save/load", "[config]") {
    TempConfig tmp;

    Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    cfg.set_obsidian_vault("/home/user/vault");
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_obsidian_vault() == "/home/user/vault");
}

TEST_CASE("Config: empty obsidian_vault is not written and loads as empty", "[config]") {
    TempConfig tmp;

    Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    cfg.set_obsidian_vault("");
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_obsidian_vault().empty());
}

// ---------------------------------------------------------------------------
// Config round-trip: topics and download_dir
// ---------------------------------------------------------------------------

TEST_CASE("Config: topics round-trip through save/load", "[config]") {
    TempConfig tmp;

    Config cfg;
    cfg.set_topics({"hep-ph", "cs.LG", "quant-ph"});
    cfg.set_download_dir("/tmp/papers");
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_topics() == std::vector<std::string>{"hep-ph", "cs.LG", "quant-ph"});
    REQUIRE(loaded.get_download_dir() == "/tmp/papers");
}
