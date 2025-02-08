#include "Arxiv/Components.hh"

#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "spdlog/spdlog.h"

using namespace ftxui;

Arxiv::ArticleListComponent::ArticleListComponent(std::vector<Article> articles,
                                                  SelectCallback on_select,
                                                  BookmarkCallback on_bookmark)
    : m_articles(std::move(articles)), m_on_select{on_select}, m_on_bookmark{on_bookmark} {

    // Create vertical container for articles
    container = Container::Vertical({});
    for(const auto &article : m_articles) {
        std::string display_text = (article.bookmarked ? "★ " : "☆ ") + article.title;
        container->Add(Button(display_text, []{}));
    }
}

bool Arxiv::ArticleListComponent::OnEvent(ftxui::Event event) {
    if(m_articles.empty()) return false;

    spdlog::trace("[ArticleList]: Got Event {}", event.character());
   
    // Handle article selection
    if(event == Event::ArrowLeft || event == Event::Character('l')) {
        m_on_select(m_articles[selected]);
        return true;
    }

    // Handle bookmark toggle
    if(event == Event::Character('b')) {
        m_on_bookmark(m_articles[selected]);
        return true;
    }

    if(event == Event::ArrowUp || event == Event::Character('k')) {
        return ScrollUp();
    }
    if(event == Event::ArrowDown || event == Event::Character('j')) {
        return ScrollDown();
    }
    if(event == Event::PageUp) {
        return PageUp();
    }
    if(event == Event::PageDown) {
        return PageDown();
    }
    return false;
}

bool Arxiv::ArticleListComponent::ScrollUp() {
    if(selected > 0) {
        selected--;
        if(selected < scroll_offset) {
            scroll_offset = selected;
        }
    }
    return true;
}

bool Arxiv::ArticleListComponent::ScrollDown() {
    if(selected < m_articles.size() - 1) {
        selected++;
        if(selected >= scroll_offset + max_visible) {
            scroll_offset = selected - max_visible + 1;
        }
    }
    return true;
}

bool Arxiv::ArticleListComponent::PageUp() {
    selected = std::max(0ul, selected - jump);
    scroll_offset = std::max(0ul, scroll_offset - jump);
    return true;
}

bool Arxiv::ArticleListComponent::PageDown() {
    selected = std::min(m_articles.size(), selected + jump);
    if(selected >= scroll_offset + max_visible) {
        scroll_offset = selected - max_visible + 1;
    }
    return true;
}

Element Arxiv::ArticleListComponent::Render() {
    std::vector<Element> entries;

    size_t start = std::min(scroll_offset, m_articles.size());
    size_t end = std::min(start + max_visible, m_articles.size());

    spdlog::debug("[ArticleList]: Rendering with start = {} and end = {}, with {} articles",
                  start, end, m_articles.size());

    for(size_t i = start; i < end && i < m_articles.size(); ++i) {
        const auto &article = m_articles[i];
        std::string display_text = (article.bookmarked ? "★ " : "☆ ") + article.title;

        auto entry = text(display_text);
        if(i == selected) {
            entry = entry | inverted;
        }
        spdlog::trace("[ArticleList]: Adding article with display text = {}", display_text);

        entries.push_back(entry);
    }

    if(entries.empty()) {
        entries.push_back(text("No articles to display") | center);
    }

    spdlog::debug("[ArticleList]: Finished render with {} entries", entries.size());

    return vbox(entries) | frame;
}
