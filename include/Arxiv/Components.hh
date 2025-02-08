#ifndef ARXIV_UI_MANAGER
#define ARXIV_UI_MANAGER

#include <vector>

#include "Arxiv/DatabaseManager.hh"
#include "ftxui/component/component.hpp"

#include "Arxiv/Fetcher.hh"

namespace Arxiv {

class ArticleListComponent : public ftxui::ComponentBase {
  public:
    using SelectCallback = std::function<void(const Article&)>;
    using BookmarkCallback = std::function<void(Article&)>;

    ArticleListComponent(std::vector<Article> articles, SelectCallback on_select,
                         BookmarkCallback on_bookmark);

    bool OnEvent(ftxui::Event event) override;
    ftxui::Element Render() override;

  private:
    static constexpr size_t max_visible = 20;
    static constexpr size_t jump = 10;

    std::vector<Article> m_articles;
    ftxui::Component container;
    size_t selected = 0;
    size_t scroll_offset = 0;
    SelectCallback m_on_select;
    BookmarkCallback m_on_bookmark;

    bool ScrollUp();
    bool ScrollDown();
    bool PageUp();
    bool PageDown();
};

class ArticleDetailView : public ftxui::ComponentBase {
  public:
    ArticleDetailView(const Article &article, std::function<void()> on_close)
        : m_article(article), m_on_close(on_close) {}

    bool OnEvent(ftxui::Event event) override;

    ftxui::Element Render() override;

  private:
    Article m_article;
    std::function<void()> m_on_close;
};

}

#endif
