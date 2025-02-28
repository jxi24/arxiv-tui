#include "Arxiv/Components.hh"

#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "spdlog/spdlog.h"

using namespace ftxui;

Arxiv::ArticleListComponent::ArticleListComponent(const std::vector<Article> &articles,
                                                  BookmarkCallback on_bookmark)
    : m_on_bookmark{on_bookmark} {

    Refresh(articles);
}

bool Arxiv::ArticleListComponent::OnEvent(ftxui::Event event) {
    if(m_titles.empty()) return false;

    spdlog::trace("[ArticleList]: Got Event {}", event.character());
   
    // Handle bookmark toggle
    if(event == Event::Character('b')) {
        m_on_bookmark(selected);
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
    if(selected < m_titles.size() - 1) {
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
    selected = std::min(m_titles.size(), selected + jump);
    if(selected >= scroll_offset + max_visible) {
        scroll_offset = selected - max_visible + 1;
    }
    return true;
}

Element Arxiv::ArticleListComponent::Render() {
    std::vector<Element> entries;

    size_t start = std::min(scroll_offset, m_titles.size());
    size_t end = std::min(start + max_visible, m_titles.size());

    spdlog::debug("[ArticleList]: Rendering with start = {} and end = {}, with {} articles",
                  start, end, m_titles.size());

    for(size_t i = start; i < end && i < m_titles.size(); ++i) {
        auto entry = text(m_titles[i]);
        if(i == selected) {
            entry = entry | inverted;
        }
        entries.push_back(entry);
    }

    if(entries.empty()) {
        entries.push_back(text("No articles to display") | center);
    }

    spdlog::debug("[ArticleList]: Finished render with {} entries", entries.size());

    return vbox(entries) | frame;
}
