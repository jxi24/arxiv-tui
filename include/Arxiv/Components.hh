#ifndef ARXIV_UI_MANAGER
#define ARXIV_UI_MANAGER

#include <vector>

#include "Arxiv/DatabaseManager.hh"
#include "ftxui/component/component.hpp"

#include "Arxiv/Fetcher.hh"

namespace Arxiv {

class ArticleListComponent : public ftxui::ComponentBase {
  public:
    using BookmarkCallback = std::function<void(size_t)>;

    ArticleListComponent(const std::vector<Article> &articles,
                         BookmarkCallback on_bookmark);

    bool OnEvent(ftxui::Event event) override;
    void Refresh(const std::vector<Article> &articles);
    ftxui::Element Render() override;

  private:
    static constexpr size_t max_visible = 20;
    static constexpr size_t jump = 10;

    std::vector<std::string> m_titles;
    ftxui::Component container;
    size_t selected = 0;
    size_t scroll_offset = 0;
    BookmarkCallback m_on_bookmark;

    bool ScrollUp();
    bool ScrollDown();
    bool PageUp();
    bool PageDown();
};

}

#endif
