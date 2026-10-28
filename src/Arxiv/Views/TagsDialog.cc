// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

#include <algorithm>

#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

// Tags for the selection (or the focused article): toggle, add, delete.
void ArxivApp::SetupTagsDialog() {
    tags_dialog = Renderer([&] {
        if (dialog_depth != Dialog::Tags)
            return emptyElement();

        const auto tags = core.GetTags();
        const std::size_t targets = core.GetSelectionCount();
        Elements lines;
        lines.push_back(text(targets > 0 ? "Tags — " + std::to_string(targets) + " selected"
                                         : "Tags — this article") |
                        bold | center | color(TextColors::primary()));
        lines.push_back(separator() | color(TextColors::border()));
        if (tags.empty())
            lines.push_back(text("  (no tags yet — press + to add one)") |
                            color(TextColors::subtext()));
        for (int i = 0; i < static_cast<int>(tags.size()); ++i) {
            const auto& tag = tags[static_cast<std::size_t>(i)];
            const auto coverage = core.TagCoverage(tag);
            const char* mark = coverage == AppCore::Coverage::All    ? "[x] "
                               : coverage == AppCore::Coverage::Some ? "[-] "
                                                                     : "[ ] ";
            auto line = text("  " + std::string(mark) + tag);
            if (i == tag_selected_index && !tag_adding && tag_pending_delete.empty())
                line = line | bold | color(TextColors::base()) | bgcolor(TextColors::primary());
            else
                line = line | color(TextColors::text());
            lines.push_back(line);
        }
        lines.push_back(separator() | color(TextColors::border()));
        if (!tag_pending_delete.empty()) {
            lines.push_back(text("Delete tag \"" + tag_pending_delete + "\" from every paper?") |
                            color(TextColors::error()));
            lines.push_back(text("Enter: delete   Esc: keep") | color(TextColors::subtext()));
        } else if (tag_adding) {
            lines.push_back(text("New tag: " + tag_new_name + "_") | color(TextColors::text()));
            lines.push_back(text("Enter: add   Esc: cancel") | color(TextColors::subtext()));
        } else {
            lines.push_back(
                text("↑/↓: move  Enter/Space: toggle  +: new tag  Del: delete tag  Esc: close") |
                color(TextColors::subtext()));
        }
        return vbox(lines) | size(WIDTH, LESS_THAN, 80) |
               borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) |
               clear_under | center;
    });
}

bool ArxivApp::HandleTagsEvent(ftxui::Event event) {
    if (!tag_pending_delete.empty()) {
        if (event == Event::Return) {
            core.RemoveTag(tag_pending_delete);
            spdlog::info("remove_tag name={}", tag_pending_delete);
            tag_pending_delete.clear();
        } else if (event == Event::Escape) {
            tag_pending_delete.clear();
        }
        tag_selected_index =
            std::max(0, std::min(tag_selected_index, static_cast<int>(core.GetTags().size()) - 1));
        return true;
    }

    if (tag_adding) {
        // Every printable key is text here, including j, k and d.
        if (event == Event::Return) {
            tag_adding = false;
            if (!tag_new_name.empty()) {
                core.ToggleTagOnTargets(tag_new_name);
                const auto tags = core.GetTags();
                tag_selected_index = static_cast<int>(
                    std::find(tags.begin(), tags.end(), tag_new_name) - tags.begin());
            }
            tag_new_name.clear();
        } else if (event == Event::Escape) {
            tag_adding = false;
            tag_new_name.clear();
        } else if (event == Event::Backspace) {
            if (!tag_new_name.empty())
                tag_new_name.pop_back();
        } else if (event.is_character()) {
            tag_new_name += event.character();
        }
        return true;
    }

    const auto tags = core.GetTags();
    const int last = static_cast<int>(tags.size()) - 1;
    if (event == Event::ArrowDown || key_bindings.matches(event, KeyBindings::Action::Next)) {
        tag_selected_index = std::min(tag_selected_index + 1, std::max(0, last));
    } else if (event == Event::ArrowUp ||
               key_bindings.matches(event, KeyBindings::Action::Previous)) {
        tag_selected_index = std::max(0, tag_selected_index - 1);
    } else if (event == Event::Character("+")) {
        tag_adding = true;
    } else if (event == Event::Escape) {
        dialog_depth = Dialog::None;
    } else if (tag_selected_index >= 0 && tag_selected_index <= last) {
        const auto& tag = tags[static_cast<std::size_t>(tag_selected_index)];
        if (event == Event::Return || event == Event::Character(" "))
            core.ToggleTagOnTargets(tag);
        else if (event == Event::Delete)
            tag_pending_delete = tag;
    }
    return true;
}
