// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupImportDialog() {
    import_dialog = Renderer([&] {
        if (dialog_depth != Dialog::Import) return emptyElement();

        return vbox({
            text("Import Project from JSON") | bold | color(TextColors::primary()),
            separator() | color(TextColors::border()),
            text("File path: " + import_path) | color(TextColors::text()),
            separator() | color(TextColors::border()),
            text("Type path, Enter to import, Esc to cancel") | color(TextColors::subtext()),
        }) | borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) | clear_under | center;
    });
}

bool ArxivApp::HandleImportEvent(ftxui::Event event) {
    if (event == Event::Return) {
        if (m_recorder) m_recorder->RecordImportProjectJSON(import_path);
        bool ok = core.ImportProjectJSON(import_path);
        dialog_depth = Dialog::None;
        if (!ok) {
            dialog_depth = Dialog::Error;
            err_msg = "Import failed: " + import_path;
        }
        import_path.clear();
        return true;
    }
    if (event == Event::Backspace) {
        if (!import_path.empty()) import_path.pop_back();
        return true;
    }
    if (event == Event::Escape) {
        dialog_depth = Dialog::None;
        import_path.clear();
        return true;
    }
    if (event.is_character()) {
        import_path += event.character();
        return true;
    }
    return true;
}
