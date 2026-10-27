// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Utf8.hh"
#include "Arxiv/Views/Colors.hh"

#include <algorithm>

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupHelpDialog() {
    help_dialog = Renderer([&] {
        if (!show_help)
            return emptyElement();

        const auto bindings = key_bindings.grouped_bindings(help_search_query);

        // Two columns of groups; each entry is "key  Action", key first so
        // the eye can scan for a letter.
        auto entry = [](const KeyBindings::BindingInfo& b) {
            return hbox({text(KeyBindings::DisplayKey(b.key)) | bold |
                             color(TextColors::primary()) | size(WIDTH, EQUAL, 7),
                         text(b.name) | color(TextColors::text())});
        };
        std::vector<Element> columns[2];
        std::size_t rows[2] = {0, 0};
        for (std::size_t i = 0; i < bindings.size();) {
            const std::string group = bindings[i].group;
            std::size_t j = i;
            while (j < bindings.size() && bindings[j].group == group)
                ++j;
            // Put the group in the shorter column, keeping groups together.
            const int col = rows[0] <= rows[1] ? 0 : 1;
            if (!columns[col].empty())
                columns[col].push_back(text(""));
            columns[col].push_back(text(group) | bold | underlined | color(TextColors::subtext()));
            for (std::size_t k = i; k < j; ++k)
                columns[col].push_back(entry(bindings[k]));
            rows[col] += (j - i) + 2;
            i = j;
        }

        std::vector<Element> dialog_content = {
            text("Key Bindings") | bold | center | color(TextColors::primary()),
            separator() | color(TextColors::border()),
        };

        // Search box
        std::string search_display =
            help_search_query.empty() ? "Filter: (type to search)" : "Filter: " + help_search_query;
        dialog_content.push_back(
            text(search_display) |
            color(help_search_query.empty() ? TextColors::subtext() : TextColors::primary()));
        dialog_content.push_back(separator() | color(TextColors::border()));

        if (bindings.empty()) {
            dialog_content.push_back(text("No bindings match \"" + help_search_query + "\"") |
                                     center | color(TextColors::subtext()));
        } else {
            dialog_content.push_back(hbox(
                {vbox(columns[0]) | size(WIDTH, GREATER_THAN, 34), text("   "), vbox(columns[1])}));
        }

        // A key bound to two actions only ever triggers one of them.
        for (const auto& c : key_bindings.Conflicts()) {
            std::string names;
            for (const auto& n : c.actions)
                names += (names.empty() ? "" : " and ") + n;
            dialog_content.push_back(
                text("⚠ " + KeyBindings::DisplayKey(c.key) + " is bound twice: " + names) |
                color(TextColors::error()));
        }

        dialog_content.push_back(separator() | color(TextColors::border()));
        dialog_content.push_back(hbox({
                                     text("Type to filter  ") | color(TextColors::subtext()),
                                     text("Backspace") | bold | color(TextColors::primary()),
                                     text(" clear  ") | color(TextColors::subtext()),
                                     text(key_bindings.get_key(KeyBindings::Action::ShowHelp)) |
                                         bold | color(TextColors::primary()),
                                     text(" close") | color(TextColors::subtext()),
                                 }) |
                                 center);

        return vbox(dialog_content) | borderStyled(ROUNDED, TextColors::border()) |
               bgcolor(TextColors::surface()) | clear_under | center;
    });
}

bool ArxivApp::HandleHelpEvent(ftxui::Event event) {
    if (key_bindings.matches(event, KeyBindings::Action::ShowHelp) || event == Event::Escape) {
        if (!help_search_query.empty()) {
            help_search_query.clear(); // first Escape clears the search
        } else {
            show_help = false;
        }
        return true;
    }
    if (event == Event::Backspace) {
        if (!help_search_query.empty())
            Utf8::PopBack(help_search_query);
        return true;
    }
    if (event.is_character()) {
        help_search_query += event.character();
        return true;
    }
    return true; // block all other events while help is shown
}
