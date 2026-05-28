// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupDateRangeDialog() {
    date_range_dialog = Renderer([&] {
        if (dialog_depth != Dialog::DateRange)
            return emptyElement();

        std::string start_prompt = "Start date (YYYY-MM-DD): " + start_date;
        std::string end_prompt = "End date (YYYY-MM-DD): " + end_date;

        if (date_input_mode == DateInputMode::Start) {
            start_prompt = "> " + start_prompt;
        } else {
            end_prompt = "> " + end_prompt;
        }

        return vbox({
                   text("Set Date Range") | bold | color(TextColors::primary()),
                   separator() | color(TextColors::border()),
                   text(start_prompt) | color(TextColors::text()),
                   text(end_prompt) | color(TextColors::text()),
                   separator() | color(TextColors::border()),
                   hbox({
                       text("Use Tab to switch between dates, Enter to save, Esc to cancel") |
                           color(TextColors::subtext()),
                   }) | center,
               }) |
               borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) |
               clear_under | center;
    });
}

bool ArxivApp::HandleDateRangeEvent(ftxui::Event event) {
    if (event == Event::Return) {
        if (!start_date.empty() && !end_date.empty()) {
            core.SetDateRange(start_date, end_date);
            if (m_recorder)
                m_recorder->RecordSetDateRange(start_date, end_date);
        }
        dialog_depth = Dialog::None;
        start_date.clear();
        end_date.clear();
        return true;
    }
    if (event.is_character()) {
        if (date_input_mode == DateInputMode::Start) {
            start_date += event.character();
        } else {
            end_date += event.character();
        }
        return true;
    }
    if (event == Event::Backspace) {
        if (date_input_mode == DateInputMode::Start) {
            if (!start_date.empty())
                start_date.pop_back();
        } else {
            if (!end_date.empty())
                end_date.pop_back();
        }
        return true;
    }
    if (event == Event::Tab) {
        date_input_mode =
            (date_input_mode == DateInputMode::Start) ? DateInputMode::End : DateInputMode::Start;
        return true;
    }
    if (event == Event::Escape) {
        dialog_depth = Dialog::None;
        start_date.clear();
        end_date.clear();
        return true;
    }
    return true;
}
