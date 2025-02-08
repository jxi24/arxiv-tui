#ifndef ARXIV_APP
#define ARXIV_APP


#include <string>
#include <vector>
#include <ftxui/component/component.hpp>

#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Components.hh"

using ftxui::Component;

namespace Arxiv {

class ArxivApp {
  public:
    ArxivApp(const std::vector<std::string> &topics);
    void Run();

  private:
    std::vector<std::string> m_topics;
    DatabaseManager db;
    Fetcher fetcher;
    Component main_container;
    Component tab_menu;
    std::shared_ptr<ArticleListComponent> article_list;
    Component detail_view;
    std::vector<Article> current_articles;
    int current_tab = 0;
    int current_view = 0;

    void InitializeUI();
    void RefreshArticles();
    void ShowDetailView(const Article &article);
    void ToggleBookmark(Article &article);
};

}


#endif
