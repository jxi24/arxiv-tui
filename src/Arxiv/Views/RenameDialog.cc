// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Rename the selected project or tag (E); move a project under another (M).

#include "Arxiv/App.hh"
#include "Arxiv/Utf8.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::OpenRenameDialog() {
    rename_is_tag = core.CurrentProject().empty();
    rename_target = rename_is_tag ? core.CurrentTag() : core.CurrentProject();
    if (rename_target.empty())
        return; // nothing to rename in this view
    rename_buffer = rename_target;
    dialog_depth = Dialog::Rename;
}

ftxui::Element ArxivApp::RenderRenameDialog() {
    const std::string what = rename_is_tag ? "tag #" : "project ";
    return vbox({
               text("Rename " + what + rename_target) | bold | color(TextColors::primary()),
               separator() | color(TextColors::border()),
               text("New name: " + rename_buffer + "_") | color(TextColors::text()),
               separator() | color(TextColors::border()),
               text("Enter renames, Esc cancels") | color(TextColors::subtext()) | center,
           }) |
           borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) |
           clear_under | center;
}

bool ArxivApp::HandleRenameEvent(ftxui::Event event) {
    if (event == Event::Escape) {
        dialog_depth = Dialog::None;
        return true;
    }
    if (event == Event::Return) {
        const bool ok = rename_is_tag ? core.RenameTag(rename_target, rename_buffer)
                                      : core.RenameProject(rename_target, rename_buffer);
        if (ok && m_recorder) {
            if (rename_is_tag)
                m_recorder->RecordRenameTag(rename_target, rename_buffer);
            else
                m_recorder->RecordRenameProject(rename_target, rename_buffer);
        }
        if (ok || rename_buffer == rename_target) {
            dialog_depth = Dialog::None;
        } else {
            err_msg = "Cannot rename to '" + rename_buffer + "': the name is empty or taken";
            dialog_depth = Dialog::Error;
        }
        return true;
    }
    if (event == Event::Backspace) {
        Utf8::PopBack(rename_buffer);
        return true;
    }
    if (event.is_character())
        rename_buffer += event.character();
    return true;
}

void ArxivApp::OpenMoveProjectDialog() {
    move_project = core.CurrentProject();
    if (move_project.empty())
        return;
    move_candidates = core.ParentCandidates(move_project);
    const auto current = core.GetProjectParent(move_project);
    const auto it = std::find(move_candidates.begin(), move_candidates.end(), current);
    move_index = it == move_candidates.end() ? 0 : static_cast<int>(it - move_candidates.begin());
    dialog_depth = Dialog::MoveProject;
}

ftxui::Element ArxivApp::RenderMoveProjectDialog() {
    Elements rows = {text("Move " + move_project + " under") | bold | color(TextColors::primary()),
                     separator() | color(TextColors::border())};
    for (int i = 0; i < static_cast<int>(move_candidates.size()); ++i) {
        const auto& name = move_candidates[static_cast<std::size_t>(i)];
        auto row = text((i == move_index ? "> " : "  ") + (name.empty() ? "(top level)" : name));
        rows.push_back(i == move_index ? row | bold | color(TextColors::primary())
                                       : row | color(TextColors::text()));
    }
    rows.push_back(separator() | color(TextColors::border()));
    rows.push_back(text("Enter moves, Esc cancels") | color(TextColors::subtext()) | center);
    return vbox(rows) | borderStyled(ROUNDED, TextColors::border()) |
           bgcolor(TextColors::surface()) | clear_under | center;
}

bool ArxivApp::HandleMoveProjectEvent(ftxui::Event event) {
    const int n = static_cast<int>(move_candidates.size());
    if (event == Event::Escape || key_bindings.matches(event, KeyBindings::Action::Quit)) {
        dialog_depth = Dialog::None;
    } else if (key_bindings.matches(event, KeyBindings::Action::Next) ||
               event == Event::ArrowDown) {
        move_index = std::min(move_index + 1, n - 1);
    } else if (key_bindings.matches(event, KeyBindings::Action::Previous) ||
               event == Event::ArrowUp) {
        move_index = std::max(move_index - 1, 0);
    } else if (event == Event::Return && move_index < n) {
        const auto& parent = move_candidates[static_cast<std::size_t>(move_index)];
        core.SetProjectParent(move_project, parent);
        if (m_recorder)
            m_recorder->RecordSetProjectParent(move_project, parent);
        dialog_depth = Dialog::None;
    }
    return true;
}
