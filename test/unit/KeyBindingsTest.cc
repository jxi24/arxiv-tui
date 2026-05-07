#include <catch2/catch_test_macros.hpp>

#include "Arxiv/Config.hh"
#include "Arxiv/KeyBindings.hh"

using Arxiv::Config;
using Arxiv::KeyBindings;
using Action = KeyBindings::Action;

TEST_CASE("KeyBindings: defaults are populated for every action", "[keybindings]") {
    KeyBindings kb{std::vector<Config::KeyMapping>{}};

    // Every action must have a non-empty default key.
    for (auto a : {Action::Next, Action::Previous, Action::Quit,
                   Action::CreateProject, Action::DeleteProject,
                   Action::DownloadArticle, Action::ShowDetail,
                   Action::MoveRight, Action::MoveLeft, Action::Bookmark,
                   Action::ShowHelp, Action::SetDateRange, Action::Search,
                   Action::RateArticle, Action::ForceRetrain, Action::EditNote,
                   Action::ExportProject, Action::ImportProject,
                   Action::ExportBibTeX, Action::EditKeywords,
                   Action::ExportDigest, Action::FilterCategories,
                   Action::ToggleSelection, Action::ExportSelectedDigest,
                   Action::ExportToObsidian}) {
        INFO("action enum value = " << static_cast<int>(a));
        REQUIRE_FALSE(kb.get_key(a).empty());
    }
}

TEST_CASE("KeyBindings: user overrides apply to every action", "[keybindings]") {
    // Each pair = (YAML action name, override key). All overrides should win
    // over the defaults. Previously only the first 8 worked; the rest were
    // silently ignored.
    const std::vector<std::pair<std::string, std::string>> overrides = {
        {"next",             "J"},
        {"previous",         "K"},
        {"quit",             "Q"},
        {"create_project",   "P"},
        {"delete_project",   "X"},
        {"download_article", "D"},
        {"show_detail",      "A"},
        {"show_help",        "H"},
        {"move_right",       "L"},
        {"move_left",        "Y"},
        {"bookmark",         "B"},
        {"set_date_range",   "R"},
        {"search",           "F"},
        {"rate_article",     "1"},
        {"force_retrain",    "2"},
        {"edit_note",        "3"},
        {"export_project",   "E"},
        {"import_project",   "i"},
        {"export_bibtex",    "C"},
        {"edit_keywords",    "W"},
        {"export_digest",    "G"},
        {"filter_categories","T"},
        {"toggle_selection", "v"},
        {"export_selected_digest","g"},
        {"export_to_obsidian","O"},
    };

    std::vector<Config::KeyMapping> mappings;
    mappings.reserve(overrides.size());
    for (const auto& [name, key] : overrides) {
        mappings.push_back({name, key});
    }

    KeyBindings kb(mappings);

    const std::vector<std::pair<std::string, Action>> action_lookup = {
        {"next",             Action::Next},
        {"previous",         Action::Previous},
        {"quit",             Action::Quit},
        {"create_project",   Action::CreateProject},
        {"delete_project",   Action::DeleteProject},
        {"download_article", Action::DownloadArticle},
        {"show_detail",      Action::ShowDetail},
        {"show_help",        Action::ShowHelp},
        {"move_right",       Action::MoveRight},
        {"move_left",        Action::MoveLeft},
        {"bookmark",         Action::Bookmark},
        {"set_date_range",   Action::SetDateRange},
        {"search",           Action::Search},
        {"rate_article",     Action::RateArticle},
        {"force_retrain",    Action::ForceRetrain},
        {"edit_note",        Action::EditNote},
        {"export_project",   Action::ExportProject},
        {"import_project",   Action::ImportProject},
        {"export_bibtex",    Action::ExportBibTeX},
        {"edit_keywords",    Action::EditKeywords},
        {"export_digest",    Action::ExportDigest},
        {"filter_categories",Action::FilterCategories},
        {"toggle_selection", Action::ToggleSelection},
        {"export_selected_digest",Action::ExportSelectedDigest},
        {"export_to_obsidian",Action::ExportToObsidian},
    };

    for (size_t i = 0; i < overrides.size(); ++i) {
        INFO("override = " << overrides[i].first);
        REQUIRE(kb.get_key(action_lookup[i].second) == overrides[i].second);
    }
}

TEST_CASE("KeyBindings: get_action_name covers every action", "[keybindings]") {
    for (auto a : {Action::Next, Action::Previous, Action::Quit,
                   Action::CreateProject, Action::DeleteProject,
                   Action::DownloadArticle, Action::ShowDetail,
                   Action::MoveRight, Action::MoveLeft, Action::Bookmark,
                   Action::ShowHelp, Action::SetDateRange, Action::Search,
                   Action::RateArticle, Action::ForceRetrain, Action::EditNote,
                   Action::ExportProject, Action::ImportProject,
                   Action::ExportBibTeX, Action::EditKeywords,
                   Action::ExportDigest, Action::FilterCategories,
                   Action::ToggleSelection, Action::ExportSelectedDigest,
                   Action::ExportToObsidian}) {
        INFO("action enum value = " << static_cast<int>(a));
        const auto name = KeyBindings::get_action_name(a);
        REQUIRE_FALSE(name.empty());
        REQUIRE(name != "Unknown");
    }
}
