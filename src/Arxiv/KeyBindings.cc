#include "Arxiv/KeyBindings.hh"
#include "Arxiv/Config.hh"

#include <array>
#include <string_view>

namespace Arxiv {

namespace {

// Single source of truth for every keybinding action.
// Adding a new Action enum value requires exactly one new row here.
struct ActionInfo {
    KeyBindings::Action action;
    std::string_view    config_name;   // YAML key under "key_mappings"
    std::string_view    default_key;
    std::string_view    display_name;  // shown in the help overlay
};

using Action = KeyBindings::Action;

constexpr std::array<ActionInfo, 26> kActionTable = {{
    {Action::Next,            "next",             "j", "Next"},
    {Action::Previous,        "previous",         "k", "Previous"},
    {Action::Quit,            "quit",             "q", "Quit"},
    {Action::CreateProject,   "create_project",   "p", "Create Project"},
    {Action::DeleteProject,   "delete_project",   "x", "Delete Project"},
    {Action::DownloadArticle, "download_article", "d", "Download Article"},
    {Action::ShowDetail,      "show_detail",      "a", "Show Detail"},
    {Action::MoveRight,       "move_right",       "l", "Move Right"},
    {Action::MoveLeft,        "move_left",        "h", "Move Left"},
    {Action::Bookmark,        "bookmark",         "b", "Bookmark"},
    {Action::ShowHelp,        "show_help",        "?", "Show Help"},
    {Action::SetDateRange,    "set_date_range",   "r", "Set Date Range"},
    {Action::Search,          "search",           "/", "Search"},
    {Action::RateArticle,     "rate_article",     "n", "Rate Article"},
    {Action::ForceRetrain,    "force_retrain",    "R", "Force Retrain"},
    {Action::EditNote,        "edit_note",        "N", "Edit Note"},
    {Action::ExportProject,   "export_project",   "e", "Export Project"},
    {Action::ImportProject,   "import_project",   "I", "Import Project"},
    {Action::ExportBibTeX,    "export_bibtex",    "c", "Export BibTeX"},
    {Action::EditKeywords,    "edit_keywords",    "K", "Edit Keywords"},
    {Action::ExportDigest,    "export_digest",    "X", "Export Digest"},
    {Action::FilterCategories,"filter_categories","t", "Filter Categories"},
    {Action::ToggleSelection, "toggle_selection", " ", "Toggle Selection"},
    {Action::ExportSelectedDigest,"export_selected_digest","g","Export Selected Digest"},
    {Action::ExportToObsidian,"export_to_obsidian","o","Export to Obsidian"},
    {Action::Settings,        "settings",          "S","Settings"},
}};

const ActionInfo* find_by_config_name(std::string_view name) {
    for (const auto& info : kActionTable) {
        if (info.config_name == name) return &info;
    }
    return nullptr;
}

const ActionInfo* find_by_action(Action a) {
    for (const auto& info : kActionTable) {
        if (info.action == a) return &info;
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

bool KeyBindings::matches(const ftxui::Event& event, Action action) const {
    if (!event.is_character()) return false;
    return event.character() == get_key(action);
}

} // namespace Arxiv
