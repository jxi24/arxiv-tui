// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupExportDialog() {
    export_dialog = Renderer([&] {
        if (dialog_depth != Dialog::Export) return emptyElement();

        static const char* formats[] = {"Markdown (.md)", "Plain Text (.txt)", "JSON (.json)", "BibTeX (.bib)"};
        Elements items;
        for (int i = 0; i < 4; ++i) {
            auto item = text(std::string(i == export_format_index ? "> " : "  ") + formats[i]);
            items.push_back(i == export_format_index
                ? item | bold | color(TextColors::primary())
                : item | color(TextColors::text()));
        }

        static const char* exts[] = {"md", "txt", "json", "bib"};
        std::string filename = export_project_name + "." + exts[export_format_index];

        return vbox({
            text("Export Project: " + export_project_name) | bold | color(TextColors::primary()),
            separator() | color(TextColors::border()),
            vbox(items),
            separator() | color(TextColors::border()),
            text("Output: " + filename) | color(TextColors::subtext()),
            separator() | color(TextColors::border()),
            text("j/k to select format, Enter to export, Esc to cancel") | color(TextColors::subtext()),
        }) | borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) | clear_under | center;
    });
}

bool ArxivApp::HandleExportEvent(ftxui::Event event) {
    if (event == Event::Return) {
        static const char* exts[] = {"md", "txt", "json", "bib"};
        std::string path = export_project_name + "." + exts[export_format_index];
        bool ok = false;
        if (export_format_index == 0) {
            ok = core.ExportProjectMarkdown(export_project_name, path);
            if (m_recorder) m_recorder->RecordExportProjectMarkdown(export_project_name, path);
        } else if (export_format_index == 1) {
            ok = core.ExportProjectText(export_project_name, path);
            if (m_recorder) m_recorder->RecordExportProjectText(export_project_name, path);
        } else if (export_format_index == 2) {
            ok = core.ExportProjectJSON(export_project_name, path);
            if (m_recorder) m_recorder->RecordExportProjectJSON(export_project_name, path);
        } else {
            ok = core.ExportProjectBibTeX(export_project_name, path);
            if (m_recorder) m_recorder->RecordExportProjectBibTeX(export_project_name, path);
        }
        if (ok) {
            dialog_depth = Dialog::Success;
            success_msg = "Exported to " + path;
        } else {
            dialog_depth = Dialog::Error;
            err_msg = "Export failed: " + path;
        }
        return true;
    }
    if (key_bindings.matches(event, KeyBindings::Action::Next)) {
        export_format_index = std::min(export_format_index + 1, 3);
        return true;
    }
    if (key_bindings.matches(event, KeyBindings::Action::Previous)) {
        export_format_index = std::max(export_format_index - 1, 0);
        return true;
    }
    if (event == Event::Escape) {
        dialog_depth = Dialog::None;
        return true;
    }
    return true;
}
