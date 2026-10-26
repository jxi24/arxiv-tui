// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SelectFilter(int index) {
    core.SetFilterIndex(index);
    if (m_recorder)
        m_recorder->RecordSetFilterIndex(core.GetFilterIndex());
    filter_menu_index = core.GetFilterIndex();
}

void ArxivApp::SetupFilterPane() {
    MenuOption option;
    option.on_change = [&] { SelectFilter(filter_menu_index); };
    // Each entry carries its count on the right; the first tag and the first
    // project carry a section header above them (headers are not entries,
    // so navigation never stops on one).
    option.entries_option.transform = [&](const EntryState& state) {
        Element label = text((state.active ? "> " : "  ") + state.label);
        if (state.focused)
            label |= inverted;
        if (state.active)
            label |= bold;
        Element row = label;
        if (auto count = core.GetFilterCount(state.index))
            row = hbox({label, filler(), text(" " + std::to_string(*count) + " ") | dim});
        const std::string section = core.GetFilterSection(state.index);
        if (section.empty())
            return row;
        return vbox({text(" " + section) | bold | color(TextColors::subtext()), row});
    };
    filter_menu =
        Menu(&core.GetFilterOptions(), &filter_menu_index, option) | CatchEvent([&](Event event) {
            filter_menu_index = core.GetFilterIndex(); // may have changed elsewhere
            if (key_bindings.matches(event, KeyBindings::Action::Next)) {
                SelectFilter(core.GetFilterIndex() + 1);
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::Previous)) {
                SelectFilter(std::max(core.GetFilterIndex() - 1, 0));
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::MoveRight)) {
                focused_pane = 1;
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::CreateProject)) {
                dialog_depth = Dialog::NewProject;
                new_project_name.clear();
                parent_for_new_project = core.CurrentProject();
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::DeleteProject)) {
                if (const std::string proj = core.CurrentProject(); !proj.empty()) {
                    core.RemoveProject(proj);
                    if (m_recorder)
                        m_recorder->RecordRemoveProject(proj);
                }
                return true;
            }
            return false;
        });

    filter_pane = Renderer(filter_menu, [&] {
        filter_menu_index = core.GetFilterIndex();
        auto header = focused_pane == 0 ? text(" Filters ") | bold | color(TextColors::base()) |
                                              bgcolor(TextColors::primary())
                                        : text(" Filters ") | bold | color(TextColors::primary());
        return vbox({header,
                     separator() | color(TextColors::border()),
                     filter_menu->Render() | vscroll_indicator | frame |
                         color(TextColors::text())}) |
               borderStyled(ROUNDED, TextColors::border()) | reflect(m_filter_box);
    });
}
