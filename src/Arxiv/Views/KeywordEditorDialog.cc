// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"
#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupKeywordEditorDialog() {
    keyword_dialog = Renderer([&] {
        if (dialog_depth != Dialog::KeywordEditor) return emptyElement();

        Elements kw_entries;
        kw_entries.push_back(text("Interest Keywords") | bold | color(TextColors::primary()));
        kw_entries.push_back(separator() | color(TextColors::border()));
        if (keyword_edit_list.empty()) {
            kw_entries.push_back(text("  (no keywords — add one below)") | color(TextColors::subtext()));
        } else {
            for (int i = 0; i < static_cast<int>(keyword_edit_list.size()); ++i) {
                bool selected = (i == keyword_selected_index);
                auto entry = text("  " + keyword_edit_list[static_cast<size_t>(i)]);
                if (selected)
                    kw_entries.push_back(entry | bold | color(TextColors::base()) | bgcolor(TextColors::primary()));
                else
                    kw_entries.push_back(entry | color(TextColors::text()));
            }
        }
        kw_entries.push_back(separator() | color(TextColors::border()));
        kw_entries.push_back(text("New: " + keyword_new_entry + "_") | color(TextColors::text()));
        kw_entries.push_back(separator() | color(TextColors::border()));
        kw_entries.push_back(
            hbox({
                text("Enter") | bold | color(TextColors::primary()),
                text(": add  ") | color(TextColors::subtext()),
                text("Del") | bold | color(TextColors::primary()),
                text(": remove selected  ") | color(TextColors::subtext()),
                text("Esc") | bold | color(TextColors::primary()),
                text(": save & close") | color(TextColors::subtext()),
            })
        );
        return vbox(std::move(kw_entries))
            | borderStyled(ROUNDED, TextColors::border())
            | bgcolor(TextColors::surface())
            | clear_under
            | center;
    });
}

bool ArxivApp::HandleKeywordEditorEvent(ftxui::Event event) {
    if (event == Event::Return && !keyword_new_entry.empty()) {
        keyword_edit_list.push_back(keyword_new_entry);
        keyword_new_entry.clear();
        return true;
    }
    if (event == Event::Delete || event == Event::Character("d")) {
        if (!keyword_edit_list.empty()) {
            keyword_edit_list.erase(
                keyword_edit_list.begin() + keyword_selected_index);
            keyword_selected_index = std::max(0,
                std::min(keyword_selected_index,
                         static_cast<int>(keyword_edit_list.size()) - 1));
        }
        return true;
    }
    if (key_bindings.matches(event, KeyBindings::Action::Next)) {
        keyword_selected_index = std::min(
            keyword_selected_index + 1,
            std::max(0, static_cast<int>(keyword_edit_list.size()) - 1));
        return true;
    }
    if (key_bindings.matches(event, KeyBindings::Action::Previous)) {
        keyword_selected_index = std::max(0, keyword_selected_index - 1);
        return true;
    }
    if (event == Event::Backspace) {
        if (!keyword_new_entry.empty()) keyword_new_entry.pop_back();
        return true;
    }
    if (event == Event::Escape) {
        if (m_recorder) m_recorder->RecordSaveKeywords(keyword_edit_list);
        spdlog::info("save_keywords count={}", keyword_edit_list.size());
        core.SaveKeywords(keyword_edit_list);
        dialog_depth = Dialog::None;
        return true;
    }
    if (event.is_character()) {
        keyword_new_entry += event.character();
        return true;
    }
    return true;
}
