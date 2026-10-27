// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/KeyBindings.hh"

#include "Arxiv/Config.hh"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>

using Arxiv::Config;
using Arxiv::KeyBindings;
using Action = KeyBindings::Action;

TEST_CASE("KeyBindings: defaults are populated for every action", "[keybindings]") {
    KeyBindings kb{std::vector<Config::KeyMapping>{}};

    // Every action must have a non-empty default key.
    for (auto a : {Action::Next,
                   Action::Previous,
                   Action::Quit,
                   Action::CreateProject,
                   Action::DeleteProject,
                   Action::DownloadArticle,
                   Action::ShowDetail,
                   Action::MoveRight,
                   Action::MoveLeft,
                   Action::Bookmark,
                   Action::ShowHelp,
                   Action::SetDateRange,
                   Action::Search,
                   Action::RateArticle,
                   Action::ForceRetrain,
                   Action::EditNote,
                   Action::ExportProject,
                   Action::ImportProject,
                   Action::ExportBibTeX,
                   Action::EditKeywords,
                   Action::ExportDigest,
                   Action::FilterCategories,
                   Action::ToggleSelection,
                   Action::ExportSelectedDigest,
                   Action::ExportToObsidian,
                   Action::Settings,
                   Action::DeleteArticle,
                   Action::UndoDelete,
                   Action::ExportDigestArchive}) {
        INFO("action enum value = " << static_cast<int>(a));
        REQUIRE_FALSE(kb.get_key(a).empty());
    }
}

TEST_CASE("KeyBindings: user overrides apply to every action", "[keybindings]") {
    // Each pair = (YAML action name, override key). All overrides should win
    // over the defaults. Previously only the first 8 worked; the rest were
    // silently ignored.
    const std::vector<std::pair<std::string, std::string>> overrides = {
        {"next", "J"},
        {"previous", "K"},
        {"quit", "Q"},
        {"create_project", "P"},
        {"delete_project", "X"},
        {"download_article", "D"},
        {"show_detail", "A"},
        {"show_help", "H"},
        {"move_right", "L"},
        {"move_left", "Y"},
        {"bookmark", "B"},
        {"set_date_range", "R"},
        {"search", "F"},
        {"rate_article", "1"},
        {"force_retrain", "2"},
        {"edit_note", "3"},
        {"export_project", "E"},
        {"import_project", "i"},
        {"export_bibtex", "C"},
        {"edit_keywords", "W"},
        {"export_digest", "G"},
        {"filter_categories", "T"},
        {"toggle_selection", "v"},
        {"export_selected_digest", "g"},
        {"export_to_obsidian", "O"},
        {"delete_article", "Z"},
        {"undo_delete", "U"},
        {"export_digest_archive", "G"},
    };

    std::vector<Config::KeyMapping> mappings;
    mappings.reserve(overrides.size());
    for (const auto& [name, key] : overrides) {
        mappings.push_back({name, key});
    }

    KeyBindings kb(mappings);

    const std::vector<std::pair<std::string, Action>> action_lookup = {
        {"next", Action::Next},
        {"previous", Action::Previous},
        {"quit", Action::Quit},
        {"create_project", Action::CreateProject},
        {"delete_project", Action::DeleteProject},
        {"download_article", Action::DownloadArticle},
        {"show_detail", Action::ShowDetail},
        {"show_help", Action::ShowHelp},
        {"move_right", Action::MoveRight},
        {"move_left", Action::MoveLeft},
        {"bookmark", Action::Bookmark},
        {"set_date_range", Action::SetDateRange},
        {"search", Action::Search},
        {"rate_article", Action::RateArticle},
        {"force_retrain", Action::ForceRetrain},
        {"edit_note", Action::EditNote},
        {"export_project", Action::ExportProject},
        {"import_project", Action::ImportProject},
        {"export_bibtex", Action::ExportBibTeX},
        {"edit_keywords", Action::EditKeywords},
        {"export_digest", Action::ExportDigest},
        {"filter_categories", Action::FilterCategories},
        {"toggle_selection", Action::ToggleSelection},
        {"export_selected_digest", Action::ExportSelectedDigest},
        {"export_to_obsidian", Action::ExportToObsidian},
        {"delete_article", Action::DeleteArticle},
        {"undo_delete", Action::UndoDelete},
        {"export_digest_archive", Action::ExportDigestArchive},
    };

    for (size_t i = 0; i < overrides.size(); ++i) {
        INFO("override = " << overrides[i].first);
        REQUIRE(kb.get_key(action_lookup[i].second) == overrides[i].second);
    }
}

TEST_CASE("KeyBindings: get_action_name covers every action", "[keybindings]") {
    for (auto a : {Action::Next,
                   Action::Previous,
                   Action::Quit,
                   Action::CreateProject,
                   Action::DeleteProject,
                   Action::DownloadArticle,
                   Action::ShowDetail,
                   Action::MoveRight,
                   Action::MoveLeft,
                   Action::Bookmark,
                   Action::ShowHelp,
                   Action::SetDateRange,
                   Action::Search,
                   Action::RateArticle,
                   Action::ForceRetrain,
                   Action::EditNote,
                   Action::ExportProject,
                   Action::ImportProject,
                   Action::ExportBibTeX,
                   Action::EditKeywords,
                   Action::ExportDigest,
                   Action::FilterCategories,
                   Action::ToggleSelection,
                   Action::ExportSelectedDigest,
                   Action::ExportToObsidian,
                   Action::Settings,
                   Action::DeleteArticle,
                   Action::UndoDelete,
                   Action::ExportDigestArchive}) {
        INFO("action enum value = " << static_cast<int>(a));
        const auto name = KeyBindings::get_action_name(a);
        REQUIRE_FALSE(name.empty());
        REQUIRE(name != "Unknown");
    }
}

TEST_CASE("KeyBindings: UndoDelete defaults to u", "[keybindings]") {
    KeyBindings kb(std::vector<Config::KeyMapping>{});
    REQUIRE(kb.get_key(KeyBindings::Action::UndoDelete) == "u");
}

TEST_CASE("KeyBindings: get_action_name returns 'Unknown' for invalid action", "[keybindings]") {
    auto name = KeyBindings::get_action_name(static_cast<KeyBindings::Action>(9999));
    REQUIRE(name == "Unknown");
}

TEST_CASE("KeyBindings: get_all_bindings returns all actions", "[keybindings]") {
    KeyBindings kb(std::vector<Arxiv::Config::KeyMapping>{});
    auto bindings = kb.get_all_bindings();
    REQUIRE_FALSE(bindings.empty());
    // Every entry should have a non-empty action name and key.
    for (const auto& [name, key] : bindings) {
        REQUIRE_FALSE(name.empty());
    }
}

TEST_CASE("KeyBindings: unknown config_name is silently ignored", "[keybindings]") {
    std::vector<Arxiv::Config::KeyMapping> mappings = {{"nonexistent_action", "z"}};
    // Should not throw; unknown names are skipped.
    REQUIRE_NOTHROW(KeyBindings(mappings));
}

TEST_CASE("KeyBindings::filter_bindings", "[keybindings][help-search]") {
    KeyBindings kb(std::vector<Config::KeyMapping>{});

    SECTION("empty query returns all bindings") {
        auto all = kb.get_all_bindings();
        auto filtered = kb.filter_bindings("");
        REQUIRE(filtered.size() == all.size());
    }

    SECTION("query matching an action name substring returns only those entries") {
        auto filtered = kb.filter_bindings("next");
        REQUIRE_FALSE(filtered.empty());
        for (const auto& [name, key] : filtered) {
            std::string lower = name;
            for (auto& c : lower)
                c = static_cast<char>(std::tolower(c));
            REQUIRE(lower.find("next") != std::string::npos);
        }
    }

    SECTION("query matching a key substring returns those entries") {
        // 'j' is the default key for Next — filter by that key
        auto filtered = kb.filter_bindings("j");
        REQUIRE_FALSE(filtered.empty());
        bool found = false;
        for (const auto& [name, key] : filtered) {
            std::string lower_key = key;
            for (auto& c : lower_key)
                c = static_cast<char>(std::tolower(c));
            if (lower_key.find("j") != std::string::npos)
                found = true;
        }
        REQUIRE(found);
    }

    SECTION("case-insensitive match on action name") {
        auto lower = kb.filter_bindings("export");
        auto upper = kb.filter_bindings("EXPORT");
        REQUIRE(lower.size() == upper.size());
    }

    SECTION("query with no match returns empty list") {
        auto filtered = kb.filter_bindings("zzz_no_such_binding_xyz");
        REQUIRE(filtered.empty());
    }
}

TEST_CASE("KeyBindings: ExportDigestArchive action exists with default key", "[keybindings]") {
    KeyBindings kb(std::vector<Config::KeyMapping>{});
    REQUIRE_FALSE(kb.get_key(Action::ExportDigestArchive).empty());
}

TEST_CASE("KeyBindings: no two actions share a default key", "[keybindings]") {
    // "c" used to be bound to both Export BibTeX and an unused Generate
    // BibTeX action, so the help overlay listed a key that did two things.
    KeyBindings kb(std::vector<Config::KeyMapping>{});
    REQUIRE(kb.Conflicts().empty());
}

TEST_CASE("KeyBindings: a user mapping that collides with another action is reported",
          "[keybindings]") {
    KeyBindings kb(std::vector<Config::KeyMapping>{{"next", "b"}}); // b = bookmark
    const auto conflicts = kb.Conflicts();
    REQUIRE(conflicts.size() == 1);
    REQUIRE(conflicts[0].key == "b");
    REQUIRE(conflicts[0].actions.size() == 2);
}

// ---------------------------------------------------------------------------
// docs/keybindings.rst must match the code: every "Action name" in its
// tables is a real config name whose default is the key shown, and every
// action is documented. (The page had drifted: copy_bibtex, focus_left, ...)
// ---------------------------------------------------------------------------

namespace {

struct DocRow {
    std::string key, action;
};

std::vector<DocRow> documented_rows() {
    const auto path =
        std::filesystem::path(__FILE__).parent_path() / ".." / ".." / "docs" / "keybindings.rst";
    std::ifstream in(path);
    REQUIRE(in.is_open());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<DocRow> rows;
    const std::regex row(R"(   \* - ``([^`]+)``\n     - ``([a-z_]+)``)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), row);
         it != std::sregex_iterator();
         ++it)
        rows.push_back({(*it)[1].str(), (*it)[2].str()});
    return rows;
}

} // namespace

TEST_CASE("Docs: every documented action name is real and shows its default key",
          "[keybindings][docs]") {
    const KeyBindings defaults(std::vector<Config::KeyMapping>{});
    for (const auto& row : documented_rows()) {
        INFO("documented: " << row.key << " -> " << row.action);
        // A real config name changes some binding when remapped.
        KeyBindings remapped(std::vector<Config::KeyMapping>{{row.action, "\x01"}});
        std::string default_key;
        bool found = false;
        const auto before = defaults.get_all_bindings();
        const auto after = remapped.get_all_bindings();
        for (std::size_t i = 0; i < after.size(); ++i)
            if (after[i].second == "\x01") {
                found = true;
                default_key = KeyBindings::DisplayKey(before[i].second);
            }
        REQUIRE(found);
        REQUIRE(default_key == row.key);
    }
}

TEST_CASE("Docs: every action is documented", "[keybindings][docs]") {
    const KeyBindings defaults(std::vector<Config::KeyMapping>{});
    REQUIRE(documented_rows().size() == defaults.get_all_bindings().size());
}

TEST_CASE("KeyBindings honours mappings saved under a display name", "[keybindings][regression]") {
    // Settings saved edited keys as "Rate Article" instead of "rate_article";
    // those entries are in users' config.yml and should take effect.
    Arxiv::KeyBindings kb({{"Rate Article", "Z"}});
    REQUIRE(kb.get_key(Arxiv::KeyBindings::Action::RateArticle) == "Z");
}
