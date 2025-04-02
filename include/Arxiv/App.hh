#ifndef ARXIV_APP
#define ARXIV_APP

#include <memory>
#include <string>
#include <vector>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <map>
#include <set>
#include <chrono>

#include "Arxiv/AppCore.hh"
#include "Arxiv/KeyBindings.hh"

using ftxui::Component;
using ftxui::Color;

namespace Arxiv {

class ArxivApp {
public:
    explicit ArxivApp(const Config &config);
    void Run() { screen.Loop(event_handler); }
    ~ArxivApp() {
        refresh_ui_continue = false;
        refresh_ui.join();
    }

private:
    // Core application logic
    AppCore core;
    KeyBindings key_bindings;
    
    // UI handling
    ftxui::ScreenInteractive screen;
    int focused_pane = 0;
    int dialog_depth = 0;
    std::string new_project_name;
    bool show_detail = false;
    bool show_help = false;
    std::set<std::string> selected_projects;
    int selected_project_index = 0;
    std::map<std::string, bool> checkbox_states;
    float title_start_position = 0;
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
    std::atomic<bool> refresh_ui_continue = true;
    std::thread refresh_ui;
    std::string err_msg = "";
    static constexpr int arrow_size = 2;
    static constexpr int padding = 4;
    static constexpr int border_size = 3;
    static constexpr float scroll_speed = 4.0f;  // Scroll speed in characters per second

    // Date range dialog
    enum class DateInputMode {
        Start,
        End
    };
    DateInputMode date_input_mode = DateInputMode::Start;
    std::string start_date;
    std::string end_date;
    Component date_range_dialog;

    // Search dialog
    std::string search_query;
    AppCore::SearchMode search_field = AppCore::SearchMode::title;  // Default to searching in title
    int selected_search_option = 0;  // 0: query, 1: title, 2: authors, 3: abstract
    Component search_dialog;

    // Article pane scrolling
    int visible_rows = 0;  // Number of rows visible in the article pane
    int top_article_index = 0;  // Index of the article at the top of the visible area

    // Components
    Component filter_menu;
    Component filter_pane;
    Component article_list;
    Component article_pane;
    Component detail_view;
    Component main_container;
    Component main_renderer;
    Component event_handler;
    Component help_dialog;
    
    // Project dialog components
    Component project_checkbox_container;
    Component project_dialog;

    // Helper functions
    void SetupUI();
    void RefreshUI();
    int FilterPaneWidth();
    void UpdateTitleScrollPositions();  // New function to handle automatic scrolling
    void UpdateVisibleRange();  // Update visible range based on selected article
    void ToggleHelp();  // Toggle the help dialog
};

} // namespace Arxiv

#endif
