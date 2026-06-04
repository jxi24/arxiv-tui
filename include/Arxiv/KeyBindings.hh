// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Arxiv/Config.hh"

#include <ftxui/component/event.hpp>

#include <map>
#include <vector>

namespace Arxiv {

class KeyBindings {
  public:
    enum class Action {
        Next,
        Previous,
        Quit,
        CreateProject,
        DeleteProject,
        DownloadArticle,
        ShowDetail,
        MoveRight,
        MoveLeft,
        Bookmark,
        ShowHelp,
        SetDateRange,
        Search,
        RateArticle,
        ForceRetrain,
        EditNote,
        ExportProject,
        ImportProject,
        ExportBibTeX,
        EditKeywords,
        ExportDigest,
        FilterCategories,
        ToggleSelection,
        ExportSelectedDigest,
        ExportToObsidian,
        Settings,
        GenerateBibtex,
        DeleteArticle,
        UndoDelete,
        ExportDigestArchive
    };

    KeyBindings() = default;
    explicit KeyBindings(const std::vector<Config::KeyMapping>& mappings);

    // Get the key for a specific action
    std::string get_key(Action action) const;

    // Get the action name as a string
    static std::string get_action_name(Action action);

    // Get all bindings as a vector of pairs (action name, key)
    std::vector<std::pair<std::string, std::string>> get_all_bindings() const;

    // Return only bindings whose action name or key contains query (case-insensitive).
    // Empty query returns all bindings.
    std::vector<std::pair<std::string, std::string>>
    filter_bindings(const std::string& query) const;

    // Check if an event matches a specific action
    bool matches(const ftxui::Event& event, Action action) const;

  private:
    std::map<Action, std::string> bindings_;

    // Initialize default bindings
    void init_defaults();
};

} // namespace Arxiv
