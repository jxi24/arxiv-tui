// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

ftxui::Element ArxivApp::RenderNewProjectDialog() {
    Elements new_proj_elements = {
        text("New Project") | bold | color(TextColors::primary()),
        separator() | color(TextColors::border()),
        text("Name: " + new_project_name) | color(TextColors::text()),
    };
    if (!parent_for_new_project.empty()) {
        new_proj_elements.push_back(
            text("Parent: " + parent_for_new_project) | color(TextColors::subtext()));
    }
    new_proj_elements.push_back(separator() | color(TextColors::border()));
    new_proj_elements.push_back(
        text("Press Enter to create, Esc to cancel") | color(TextColors::subtext()) | center);

    return vbox(new_proj_elements)
        | borderStyled(ROUNDED, TextColors::border())
        | bgcolor(TextColors::surface())
        | clear_under
        | center;
}

bool ArxivApp::HandleNewProjectEvent(ftxui::Event event) {
    if (event == Event::Return) {
        if (!new_project_name.empty()) {
            core.AddProject(new_project_name);
            if (m_recorder) m_recorder->RecordAddProject(new_project_name);
            if (!parent_for_new_project.empty()) {
                core.SetProjectParent(new_project_name, parent_for_new_project);
                if (m_recorder) m_recorder->RecordSetProjectParent(new_project_name, parent_for_new_project);
            }
        }
        dialog_depth = Dialog::None;
        return true;
    }
    if (event.is_character()) {
        new_project_name += event.character();
        return true;
    }
    if (event == Event::Backspace) {
        if (!new_project_name.empty()) {
            new_project_name.pop_back();
        }
        return true;
    }
    return false;
}
