#include "Arxiv/KeyBindings.hh"
#include "Arxiv/Config.hh"
#include <algorithm>

namespace Arxiv {

KeyBindings::KeyBindings(const std::vector<Config::KeyMapping>& mappings) {
    init_defaults();
    
    // Override defaults with user mappings
    for (const auto& mapping : mappings) {
        if (mapping.action == "next") bindings_[Action::Next] = mapping.key;
        else if (mapping.action == "previous") bindings_[Action::Previous] = mapping.key;
        else if (mapping.action == "quit") bindings_[Action::Quit] = mapping.key;
        else if (mapping.action == "create_project") bindings_[Action::CreateProject] = mapping.key;
        else if (mapping.action == "delete_project") bindings_[Action::DeleteProject] = mapping.key;
        else if (mapping.action == "download_article") bindings_[Action::DownloadArticle] = mapping.key;
        else if (mapping.action == "show_detail") bindings_[Action::ShowDetail] = mapping.key;
        else if (mapping.action == "show_help") bindings_[Action::ShowHelp] = mapping.key;
    }
}

void KeyBindings::init_defaults() {
    bindings_ = {
        {Action::Next, "j"},
        {Action::Previous, "k"},
        {Action::Quit, "q"},
        {Action::CreateProject, "p"},
        {Action::DeleteProject, "x"},
        {Action::DownloadArticle, "d"},
        {Action::ShowDetail, "a"},
        {Action::MoveRight, "l"},
        {Action::MoveLeft, "h"},
        {Action::Bookmark, "b"},
        {Action::ShowHelp, "?"}
    };
}

std::string KeyBindings::get_key(Action action) const {
    auto it = bindings_.find(action);
    return it != bindings_.end() ? it->second : "";
}

std::string KeyBindings::get_action_name(Action action) {
    switch (action) {
        case Action::Next: return "Next";
        case Action::Previous: return "Previous";
        case Action::Quit: return "Quit";
        case Action::CreateProject: return "Create Project";
        case Action::DeleteProject: return "Delete Project";
        case Action::DownloadArticle: return "Download Article";
        case Action::ShowDetail: return "Show Detail";
        case Action::MoveRight: return "Move Right";
        case Action::MoveLeft: return "Move Left";
        case Action::Bookmark: return "Bookmark";
        case Action::ShowHelp: return "Show Help";
        default: return "Unknown";
    }
}

std::vector<std::pair<std::string, std::string>> KeyBindings::get_all_bindings() const {
    std::vector<std::pair<std::string, std::string>> result;
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
