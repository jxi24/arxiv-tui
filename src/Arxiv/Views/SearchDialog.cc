#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"
#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupSearchDialog() {
    search_dialog = Renderer([&] {
        if (dialog_depth != Dialog::Search) return emptyElement();

        std::vector<Element> elements = {
            text("Search Articles") | bold | color(TextColors::primary()),
            separator() | color(TextColors::border()),
        };

        if (selected_search_option == 0) {
            elements.push_back(text("> Query: " + search_query) | color(TextColors::primary()));
        } else {
            elements.push_back(text("  Query: " + search_query) | color(TextColors::text()));
        }

        elements.push_back(text("Search in:") | color(TextColors::text()));
        elements.push_back(text("  [" + std::string(search_field == AppCore::SearchMode::title ? "X" : " ") + "] Title") | color(TextColors::text()));
        elements.push_back(text("  [" + std::string(search_field == AppCore::SearchMode::authors ? "X" : " ") + "] Authors") | color(TextColors::text()));
        elements.push_back(text("  [" + std::string(search_field == AppCore::SearchMode::abstract ? "X" : " ") + "] Abstract") | color(TextColors::text()));

        elements.push_back(separator() | color(TextColors::border()));
        elements.push_back(
            hbox({
                text("Use Tab to move, Space to toggle, Enter to search, Esc to cancel") | color(TextColors::subtext()),
            }) | center
        );

        return vbox(elements) | borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) | clear_under | center;
    });
}

bool ArxivApp::HandleSearchEvent(ftxui::Event event) {
    if (event == Event::Return) {
        if (!search_query.empty()) {
            bool st  = (search_field == AppCore::SearchMode::title);
            bool sa  = (search_field == AppCore::SearchMode::authors);
            bool sab = (search_field == AppCore::SearchMode::abstract);
            spdlog::info("search query=\"{}\" title={} authors={} abstract={}", search_query, st, sa, sab);
            core.SetSearchQuery(search_query, st, sa, sab);
            core.SetFilterIndex(AppCore::FilterView::Search);
            if (m_recorder) {
                m_recorder->RecordSetSearchQuery(search_query, st, sa, sab);
                m_recorder->RecordSetFilterIndex(static_cast<int>(AppCore::FilterView::Search));
            }
        }
        dialog_depth = Dialog::None;
        search_query.clear();
        selected_search_option = 0;
        return true;
    }
    if (event.is_character() && selected_search_option == 0) {
        search_query += event.character();
        return true;
    }
    if (event == Event::Backspace && selected_search_option == 0) {
        if (!search_query.empty()) search_query.pop_back();
        return true;
    }
    if (event == Event::Tab) {
        selected_search_option = (selected_search_option + 1) % 4;
        return true;
    }
    if (event == Event::Character(' ')) {
        if (selected_search_option > 0) {
            switch (selected_search_option) {
                case 1: search_field = AppCore::SearchMode::title; break;
                case 2: search_field = AppCore::SearchMode::authors; break;
                case 3: search_field = AppCore::SearchMode::abstract; break;
            }
        }
        return true;
    }
    if (event == Event::Escape) {
        dialog_depth = Dialog::None;
        search_query.clear();
        selected_search_option = 0;
        return true;
    }
    return true;
}
