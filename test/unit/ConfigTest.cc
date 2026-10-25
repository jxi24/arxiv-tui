// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Config.hh"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

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
// Config: keywords_file (interest keywords for ranking; shared with other tools)
// ---------------------------------------------------------------------------

TEST_CASE("Config: keywords_file is read from the YAML", "[config]") {
    TempConfig tmp;
    {
        std::ofstream out(tmp.path);
        out << "article_settings:\n"
               "  download_dir: /tmp\n"
               "  topics:\n"
               "    - hep-ph\n"
               "keywords_file: /home/user/vault/Utility/arxiv/keywords.txt\n";
    }
    Config loaded(tmp.path);
    REQUIRE(loaded.get_keywords_file() == "/home/user/vault/Utility/arxiv/keywords.txt");
}

TEST_CASE("Config: keywords_file round-trips through save/load", "[config]") {
    TempConfig tmp;

    Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    cfg.set_keywords_file("/tmp/keywords.txt");
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_keywords_file() == "/tmp/keywords.txt");
}

TEST_CASE("Config: empty keywords_file is not written and loads as empty", "[config]") {
    TempConfig tmp;

    Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    cfg.save_to_file(tmp.path);

    std::ifstream in(tmp.path);
    std::string yaml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(yaml.find("keywords_file") == std::string::npos);
    REQUIRE(Config(tmp.path).get_keywords_file().empty());
}

// ---------------------------------------------------------------------------
// Config: obsidian_download_pdfs (off by default; PDFs stay on arXiv)
// ---------------------------------------------------------------------------

TEST_CASE("Config: obsidian_download_pdfs defaults to false", "[config]") {
    Config cfg;
    REQUIRE_FALSE(cfg.get_obsidian_download_pdfs());
}

TEST_CASE("Config: obsidian_download_pdfs round-trips through save/load", "[config]") {
    TempConfig tmp;

    Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    cfg.set_obsidian_download_pdfs(true);
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_obsidian_download_pdfs());
}

TEST_CASE("Config: config file without obsidian_download_pdfs loads as false", "[config]") {
    TempConfig tmp;
    {
        std::ofstream f(tmp.path);
        f << "article_settings:\n"
             "  download_dir: /tmp\n"
             "  topics:\n"
             "    - hep-ph\n"
             "obsidian_vault: /tmp/vault\n";
    }

    Config loaded(tmp.path);
    REQUIRE_FALSE(loaded.get_obsidian_download_pdfs());
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

// ---------------------------------------------------------------------------
// Config: max_article_age_days
// ---------------------------------------------------------------------------

TEST_CASE("Config: max_article_age_days defaults to 0", "[config]") {
    TempConfig tmp;
    Config cfg;
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_max_article_age_days() == 0);
}

TEST_CASE("Config: max_article_age_days round-trips through save/load", "[config]") {
    TempConfig tmp;
    Config cfg;
    cfg.set_max_article_age_days(30);
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_max_article_age_days() == 30);
}

TEST_CASE("Config: article_columns defaults to title and date", "[config]") {
    Config cfg;
    auto cols = cfg.get_article_columns();
    REQUIRE_FALSE(cols.empty());
    // title must always be present in defaults
    bool has_title = std::find(cols.begin(), cols.end(), "title") != cols.end();
    REQUIRE(has_title);
}

TEST_CASE("Config: article_columns round-trips through save/load", "[config]") {
    TempConfig tmp;
    Config cfg;
    cfg.set_article_columns({"title", "authors", "date", "category"});
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_article_columns() ==
            std::vector<std::string>{"title", "authors", "date", "category"});
}

TEST_CASE("Config: article_columns absent from file gives default", "[config]") {
    TempConfig tmp;
    // Write a minimal config that omits article_columns entirely
    std::ofstream f(tmp.path);
    f << "article_settings:\n  download_dir: /tmp\n  topics:\n    - cs.AI\n";
    f.close();

    Config loaded(tmp.path);
    auto cols = loaded.get_article_columns();
    REQUIRE_FALSE(cols.empty());
    bool has_title = std::find(cols.begin(), cols.end(), "title") != cols.end();
    REQUIRE(has_title);
}

// ---------------------------------------------------------------------------
// Regressions found in code review — each fails until the bug is fixed.
// A hand-edited config with a bad value must not crash the app at startup;
// the bad key falls back to its default.
// ---------------------------------------------------------------------------

TEST_CASE("Config regression: a wrongly typed value falls back to its default",
          "[config][regression]") {
    TempConfig tmp;
    {
        std::ofstream f(tmp.path);
        f << "article_settings:\n"
          << "  download_dir: /tmp\n"
          << "  topics: [cs.AI]\n"
          << "retrain_interval: five\n";
    }

    std::optional<Config> loaded;
    REQUIRE_NOTHROW(loaded.emplace(tmp.path));
    REQUIRE(loaded->get_retrain_interval() == 5);
}

TEST_CASE("Config regression: article_settings without download_dir loads",
          "[config][regression]") {
    TempConfig tmp;
    {
        std::ofstream f(tmp.path);
        f << "article_settings:\n"
          << "  topics: [cs.AI]\n";
    }

    std::optional<Config> loaded;
    REQUIRE_NOTHROW(loaded.emplace(tmp.path));
    REQUIRE(loaded->get_topics() == std::vector<std::string>{"cs.AI"});
}

TEST_CASE("Config regression: out-of-range numbers fall back to their defaults",
          "[config][regression]") {
    TempConfig tmp;
    {
        std::ofstream f(tmp.path);
        f << "article_settings:\n"
          << "  download_dir: /tmp\n"
          << "  topics: [cs.AI]\n"
          << "retrain_interval: 0\n"
          << "undo_buffer_size: -1\n";
    }

    std::optional<Config> loaded;
    REQUIRE_NOTHROW(loaded.emplace(tmp.path));

    SECTION("retrain_interval must be positive") { REQUIRE(loaded->get_retrain_interval() == 5); }
    SECTION("undo_buffer_size must not wrap around") {
        REQUIRE(loaded->get_undo_buffer_size() == 10);
    }
}

TEST_CASE("Config: theme and colour overrides round-trip", "[config][theme]") {
    TempConfig tmp;
    Config cfg;
    REQUIRE(cfg.get_theme() == "frappe"); // default: the original palette
    cfg.set_theme("latte");
    cfg.set_theme_colors({{"primary", "#112233"}});
    cfg.save_to_file(tmp.path);

    Config loaded(tmp.path);
    REQUIRE(loaded.get_theme() == "latte");
    REQUIRE(loaded.get_theme_colors().at("primary") == "#112233");
}

// ---------------------------------------------------------------------------
// Bad config: every rejected value is reported (the TUI shows them in a
// pop-up) and the default is used instead.
// ---------------------------------------------------------------------------

TEST_CASE("Config: rejected values are reported as warnings", "[config][warnings]") {
    TempConfig tmp;
    {
        std::ofstream f(tmp.path);
        f << "article_settings:\n"
          << "  download_dir: /tmp\n"
          << "  topics: [cs.AI]\n"
          << "retrain_interval: five\n"
          << "scroll_margin: -4\n";
    }
    Config loaded(tmp.path);
    const auto& warnings = loaded.get_warnings();

    REQUIRE(warnings.size() == 2);
    REQUIRE_THAT(warnings[0], Catch::Matchers::ContainsSubstring("retrain_interval"));
    REQUIRE_THAT(warnings[1], Catch::Matchers::ContainsSubstring("scroll_margin"));
    REQUIRE(loaded.get_retrain_interval() == 5);
    REQUIRE(loaded.get_scroll_margin() == 3);
}

TEST_CASE("Config: a valid file produces no warnings", "[config][warnings]") {
    TempConfig tmp;
    Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.save_to_file(tmp.path);
    REQUIRE(Config(tmp.path).get_warnings().empty());
}

TEST_CASE("Config: an unparseable file loads the defaults with a warning", "[config][warnings]") {
    TempConfig tmp;
    {
        std::ofstream f(tmp.path);
        f << "article_settings: [unclosed\n  topics: {\n";
    }
    std::optional<Config> loaded;
    REQUIRE_NOTHROW(loaded.emplace(tmp.path));
    REQUIRE(loaded->get_topics() ==
            std::vector<std::string>{"hep-ph", "hep-ex", "hep-lat", "hep-th"});
    REQUIRE(loaded->get_warnings().size() == 1);
    REQUIRE_THAT(loaded->get_warnings()[0], Catch::Matchers::ContainsSubstring("defaults"));
}
