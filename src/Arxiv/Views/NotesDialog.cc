// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupNotesDialog() {
    note_dialog = Renderer([&] {
        if (dialog_depth != Dialog::Notes)
            return emptyElement();

        return vbox({
                   text("Edit Note") | bold | color(TextColors::primary()),
                   separator() | color(TextColors::border()),
                   paragraph(note_article_link) | color(TextColors::subtext()),
                   separator() | color(TextColors::border()),
                   text("Note: " + note_edit_text) | color(TextColors::text()),
                   separator() | color(TextColors::border()),
                   text("Type to edit, Enter to save, Esc to cancel") |
                       color(TextColors::subtext()),
               }) |
               borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) |
               clear_under | center;
    });
}

bool ArxivApp::HandleNotesEvent(ftxui::Event event) {
    if (event == Event::Return) {
        core.SetProjectNote(note_project_name, note_article_link, note_edit_text);
        if (m_recorder)
            m_recorder->RecordSetProjectNote(note_project_name, note_article_link, note_edit_text);
        dialog_depth = Dialog::None;
        return true;
    }
    if (event == Event::Backspace) {
        if (!note_edit_text.empty())
            note_edit_text.pop_back();
        return true;
    }
    if (event == Event::Escape) {
        dialog_depth = Dialog::None;
        return true;
    }
    if (event.is_character()) {
        note_edit_text += event.character();
        return true;
    }
    return true;
}
