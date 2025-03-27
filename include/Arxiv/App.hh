#ifndef ARXIV_APP
#define ARXIV_APP

#include <memory>
#include <string>
#include <vector>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <map>
#include <set>

#include "Arxiv/AppCore.hh"

using ftxui::Component;

namespace Arxiv {

class ArxivApp {
public:
    explicit ArxivApp(const std::vector<std::string>& topics);
    void Run() { screen.Loop(event_handler); }

private:
    // Core application logic
    AppCore core;
    
    // UI handling
    ftxui::ScreenInteractive screen;
    int focused_pane = 0;
    int dialog_depth = 0;
    std::string new_project_name;
    bool show_detail = false;
    std::set<std::string> selected_projects;
    int selected_project_index = 0;
    std::map<std::string, bool> checkbox_states;
    static constexpr int arrow_size = 2;
    static constexpr int padding = 4;
    static constexpr int border_size = 3;

    // Components
    Component filter_menu;
    Component filter_pane;
    Component article_list;
    Component article_pane;
    Component detail_view;
    Component main_container;
    Component main_renderer;
    Component event_handler;
    
    // Project dialog components
    Component project_checkbox_container;
    Component project_dialog;

    // Helper functions
    void SetupUI();
    void RefreshUI();
    int FilterPaneWidth();
};

} // namespace Arxiv

#endif
