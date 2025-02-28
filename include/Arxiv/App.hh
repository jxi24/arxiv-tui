#ifndef ARXIV_APP
#define ARXIV_APP


#include <string>
#include <vector>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Components.hh"

using ftxui::Component;

namespace Arxiv {

class ArxivApp {
  public:
    ArxivApp(const std::vector<std::string> &topics);
    void Run() { screen.Loop(event_handler); }

  private:
    // Data handling
    std::vector<std::string> m_topics;
    std::unique_ptr<DatabaseManager> db;
    Fetcher fetcher;
    void FetchArticles();
    std::vector<std::string> current_titles;
    std::vector<Article> current_articles;
    std::vector<std::string> projects;

    // UI handling
    ftxui::ScreenInteractive screen;
    int filter_index = 0;
    int article_index = 0;
    int focused_pane = 0;
    bool show_detail = false;
    bool needs_refresh = true;
    static constexpr int arrow_size = 2;
    static constexpr int padding = 4;
    static constexpr int border_size = 3;

    // Options
    std::vector<std::string> filter_options;

    // Components
    Component filter_menu;
    Component filter_pane;
    Component article_list;
    Component article_pane;
    Component detail_view;
    Component main_container;
    Component main_renderer;
    Component event_handler;

    // Helper functions
    void SetupUI();
    void RefreshTitles();
    void ToggleBookmark(Article &article);
    int FilterPaneWidth();

    // Project management
    void AddProject();
    void AddArticleToProjects();
    void RefreshFilterOptions();
};

}


#endif
