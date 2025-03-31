#pragma once

#include <map>
#include <vector>
#include <ftxui/component/event.hpp>

#include "Arxiv/Config.hh"

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
        ShowHelp
    };

    KeyBindings() = default;
    explicit KeyBindings(const std::vector<Config::KeyMapping>& mappings);

    // Get the key for a specific action
    std::string get_key(Action action) const;
    
    // Get the action name as a string
    static std::string get_action_name(Action action);
    
    // Get all bindings as a vector of pairs (action name, key)
    std::vector<std::pair<std::string, std::string>> get_all_bindings() const;
    
    // Check if an event matches a specific action
    bool matches(const ftxui::Event& event, Action action) const;

private:
    std::map<Action, std::string> bindings_;
    
    // Initialize default bindings
    void init_defaults();
};

} // namespace Arxiv 
