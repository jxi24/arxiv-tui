// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupSearchDialog() {
    search_dialog = Renderer([&] {
        if (dialog_depth != Dialog::Search)
            return emptyElement();

        std::vector<Element> elements = {
            text("Search Articles") | bold | color(TextColors::primary()),
            separator() | color(TextColors::border()),
        };

        // Query line is focused (and highlighted) in text-entry mode.
        if (!search_area_selection) {
            elements.push_back(text("> Query: " + search_query) | color(TextColors::primary()));
        } else {
            elements.push_back(text("  Query: " + search_query) | color(TextColors::text()));
        }

        elements.push_back(text("Search in:") | color(TextColors::text()));

        // Render each search area. In area-selection mode the cursor (">") marks
        // the highlighted area; "[X]" marks the active search field in both modes.
        auto area_line = [&](int index, AppCore::SearchMode mode, const std::string& label) {
            bool is_cursor = search_area_selection && search_area_index == index;
            bool is_active = (search_field == mode);
            std::string line =
                std::string(is_cursor ? "> " : "  ") + "[" + (is_active ? "X" : " ") + "] " + label;
            return text(line) | color(is_cursor ? TextColors::primary() : TextColors::text());
        };
        elements.push_back(area_line(0, AppCore::SearchMode::title, "Title"));
        elements.push_back(area_line(1, AppCore::SearchMode::authors, "Authors"));
        elements.push_back(area_line(2, AppCore::SearchMode::abstract, "Abstract"));

        elements.push_back(separator() | color(TextColors::border()));
        std::string hint =
            search_area_selection
                ? "Up/Down to move, Space to toggle, Tab for text entry, Enter to search, Esc to "
                  "cancel"
                : "Type to edit query, Tab to pick search area, Enter to search, Esc to cancel";
        elements.push_back(hbox({
                               text(hint) | color(TextColors::subtext()),
                           }) |
                           center);

        // Fixed inner width keeps the box the same size in both modes, where the
        // hint lines differ in length (longest hint is 84 columns).
        return vbox(elements) | size(WIDTH, EQUAL, 84) |
               borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) |
               clear_under | center;
    });
}

bool ArxivApp::HandleSearchEvent(ftxui::Event event) {
    if (event == Event::Return) {
        if (!search_query.empty()) {
            bool st = (search_field == AppCore::SearchMode::title);
            bool sa = (search_field == AppCore::SearchMode::authors);
            bool sab = (search_field == AppCore::SearchMode::abstract);
            spdlog::info(
                "search query=\"{}\" title={} authors={} abstract={}", search_query, st, sa, sab);
            core.SetSearchQuery(search_query, st, sa, sab);
            core.SetFilterIndex(AppCore::FilterView::Search);
            if (m_recorder) {
                m_recorder->RecordSetSearchQuery(search_query, st, sa, sab);
                m_recorder->RecordSetFilterIndex(static_cast<int>(AppCore::FilterView::Search));
            }
        }
        dialog_depth = Dialog::None;
        search_query.clear();
        search_area_selection = false;
        return true;
    }

    // Esc exits the dialog from either mode.
    if (event == Event::Escape) {
        dialog_depth = Dialog::None;
        search_query.clear();
        search_area_selection = false;
        return true;
    }

    // Tab toggles between text entry and search-area selection.
    if (event == Event::Tab) {
        search_area_selection = !search_area_selection;
        return true;
    }

    if (!search_area_selection) {
        // Text-entry mode: typing edits the query.
        if (event.is_character()) {
            search_query += event.character();
            return true;
        }
        if (event == Event::Backspace) {
            if (!search_query.empty())
                search_query.pop_back();
            return true;
        }
        return true;
    }

    // Search-area selection mode: navigate areas and toggle the active field.
    constexpr int area_count = 3;
    if (key_bindings.matches(event, KeyBindings::Action::Next) || event == Event::ArrowDown) {
        search_area_index = (search_area_index + 1) % area_count;
        return true;
    }
    if (key_bindings.matches(event, KeyBindings::Action::Previous) || event == Event::ArrowUp) {
        search_area_index = (search_area_index + area_count - 1) % area_count;
        return true;
    }
    if (event == Event::Character(' ')) {
        switch (search_area_index) {
        case 0:
            search_field = AppCore::SearchMode::title;
            break;
        case 1:
            search_field = AppCore::SearchMode::authors;
            break;
        case 2:
            search_field = AppCore::SearchMode::abstract;
            break;
        }
        return true;
    }
    return true;
}
