// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/KeyBindings.hh"

#include "Arxiv/Config.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace Arxiv {

namespace {

// Single source of truth for every keybinding action.
// Adding a new Action enum value requires exactly one new row here.
struct ActionInfo {
    KeyBindings::Action action;
    std::string_view config_name; // YAML key under "key_mappings"
    std::string_view default_key;
    std::string_view display_name; // shown in the help overlay
    std::string_view group;        // help overlay section
};

using Action = KeyBindings::Action;

constexpr std::array<ActionInfo, 31> kActionTable = {{
    {Action::Next, "next", "j", "Next", "Navigation"},
    {Action::Previous, "previous", "k", "Previous", "Navigation"},
    {Action::Quit, "quit", "q", "Quit", "App"},
    {Action::CreateProject, "create_project", "p", "Create Project", "Projects"},
    {Action::DeleteProject, "delete_project", "x", "Delete Project", "Projects"},
    {Action::DownloadArticle, "download_article", "d", "Download Article", "Articles"},
    {Action::ShowDetail, "show_detail", "a", "Show Detail", "Navigation"},
    {Action::MoveRight, "move_right", "l", "Move Right", "Navigation"},
    {Action::MoveLeft, "move_left", "h", "Move Left", "Navigation"},
    {Action::Bookmark, "bookmark", "b", "Bookmark", "Articles"},
    {Action::ShowHelp, "show_help", "?", "Show Help", "App"},
    {Action::SetDateRange, "set_date_range", "r", "Set Date Range", "Navigation"},
    {Action::Search, "search", "/", "Search", "Navigation"},
    {Action::RateArticle, "rate_article", "n", "Rate Article", "Articles"},
    {Action::ForceRetrain, "force_retrain", "R", "Force Retrain", "Ranking"},
    {Action::EditNote, "edit_note", "N", "Edit Note", "Articles"},
    {Action::ExportProject, "export_project", "e", "Export Project", "Projects"},
    {Action::ImportProject, "import_project", "I", "Import Project", "Projects"},
    {Action::ExportBibTeX, "export_bibtex", "c", "Export BibTeX", "Export & share"},
    {Action::EditKeywords, "edit_keywords", "K", "Edit Keywords", "Ranking"},
    {Action::ExportDigest, "export_digest", "X", "Export Digest", "Export & share"},
    {Action::FilterCategories, "filter_categories", "t", "Filter Categories", "Navigation"},
    {Action::ToggleSelection, "toggle_selection", " ", "Toggle Selection", "Selection"},
    {Action::ExportSelectedDigest,
     "export_selected_digest",
     "g",
     "Export Selected Digest",
     "Export & share"},
    {Action::ExportToObsidian, "export_to_obsidian", "o", "Export to Obsidian", "Export & share"},
    {Action::Settings, "settings", "S", "Settings", "App"},
    {Action::DeleteArticle, "delete_article", "D", "Delete Article", "Articles"},
    {Action::UndoDelete, "undo_delete", "u", "Undo Delete", "Articles"},
    {Action::ExportDigestArchive,
     "export_digest_archive",
     "G",
     "Export Digest Archive",
     "Export & share"},
    {Action::OpenInBrowser, "open_in_browser", "O", "Open in Browser", "Navigation"},
    {Action::RateSelection, "rate_selection", "W", "Rate Selection", "Selection"},
}};

const ActionInfo* find_by_config_name(std::string_view name) {
    for (const auto& info : kActionTable) {
        if (info.config_name == name)
            return &info;
    }
    return nullptr;
}

const ActionInfo* find_by_action(Action a) {
    for (const auto& info : kActionTable) {
        if (info.action == a)
            return &info;
    }
    return nullptr;
}

} // namespace

KeyBindings::KeyBindings(const std::vector<Config::KeyMapping>& mappings) {
    init_defaults();
    for (const auto& m : mappings) {
        if (const auto* info = find_by_config_name(m.action)) {
            bindings_[info->action] = m.key;
        }
    }
}

void KeyBindings::init_defaults() {
    bindings_.clear();
    for (const auto& info : kActionTable) {
        bindings_.emplace(info.action, std::string(info.default_key));
    }
}

std::string KeyBindings::get_key(Action action) const {
    auto it = bindings_.find(action);
    return it != bindings_.end() ? it->second : "";
}

std::string KeyBindings::get_action_name(Action action) {
    if (const auto* info = find_by_action(action)) {
        return std::string(info->display_name);
    }
    return "Unknown";
}

std::vector<std::pair<std::string, std::string>> KeyBindings::get_all_bindings() const {
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(bindings_.size());
    for (const auto& [action, key] : bindings_) {
        result.emplace_back(get_action_name(action), key);
    }
    return result;
}

std::vector<std::pair<std::string, std::string>>
KeyBindings::filter_bindings(const std::string& query) const {
    if (query.empty())
        return get_all_bindings();

    std::string lower_query = query;
    for (auto& c : lower_query)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& [action, key] : bindings_) {
        std::string name = get_action_name(action);
        std::string lower_name = name;
        for (auto& c : lower_name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string lower_key = key;
        for (auto& c : lower_key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower_name.find(lower_query) != std::string::npos ||
            lower_key.find(lower_query) != std::string::npos) {
            result.emplace_back(name, key);
        }
    }
    return result;
}

bool KeyBindings::matches(const ftxui::Event& event, Action action) const {
    if (!event.is_character())
        return false;
    return event.character() == get_key(action);
}

std::vector<KeyBindings::BindingInfo>
KeyBindings::grouped_bindings(const std::string& query) const {
    static constexpr std::array<std::string_view, 7> kGroupOrder = {
        "Navigation", "Articles", "Selection", "Projects", "Export & share", "Ranking", "App"};
    std::vector<BindingInfo> result;
    for (const auto& [name, key] : filter_bindings(query)) {
        const ActionInfo* info = nullptr;
        for (const auto& row : kActionTable)
            if (row.display_name == name)
                info = &row;
        result.push_back({info ? std::string(info->group) : "Other", name, key});
    }
    auto rank = [](const std::string& group) {
        auto it = std::find(kGroupOrder.begin(), kGroupOrder.end(), group);
        return static_cast<std::size_t>(it - kGroupOrder.begin());
    };
    std::stable_sort(result.begin(), result.end(), [&](const auto& a, const auto& b) {
        return rank(a.group) < rank(b.group);
    });
    return result;
}

std::string KeyBindings::DisplayKey(const std::string& key) { return key == " " ? "Space" : key; }

std::vector<KeyBindings::Conflict> KeyBindings::Conflicts() const {
    std::map<std::string, std::vector<std::string>> by_key;
    for (const auto& [action, key] : bindings_)
        if (!key.empty())
            by_key[key].push_back(get_action_name(action));
    std::vector<Conflict> conflicts;
    for (auto& [key, actions] : by_key)
        if (actions.size() > 1)
            conflicts.push_back({key, std::move(actions)});
    return conflicts;
}

} // namespace Arxiv
