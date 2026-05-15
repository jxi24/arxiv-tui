#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

#include <algorithm>

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupHelpDialog() {
    help_dialog = Renderer([&] {
        if (!show_help) return emptyElement();

        auto bindings = key_bindings.get_all_bindings();

        for (auto& [_, key] : bindings) {
            if (key == " ") key = "<space>";
        }

        std::sort(bindings.begin(), bindings.end(),
                 [](const auto& a, const auto& b) { return a.first < b.first; });

        int term_width = Terminal::Size().dimx;
        int dialog_width = std::min(term_width - 4, 80);
        int column_width = (dialog_width - 4) / 3;

        std::vector<Element> columns[3];
        for (size_t i = 0; i < bindings.size(); ++i) {
            const auto& [action, key] = bindings[i];
            columns[i % 3].push_back(
                hbox({
                    text(action) | bold | color(TextColors::primary()),
                    text(": ") | color(TextColors::primary()),
                    text(key) | color(TextColors::subtext())
                }) | size(WIDTH, EQUAL, column_width)
            );
        }

        std::vector<Element> dialog_content = {
            text("Key Bindings") | bold | center | color(TextColors::primary()),
            separator() | color(TextColors::border()),
        };

        for (size_t i = 0; i < columns[0].size(); ++i) {
            Elements row;
            for (int col = 0; col < 3; ++col) {
                if (i < columns[col].size()) {
                    row.push_back(columns[col][i]);
                } else {
                    row.push_back(text("") | size(WIDTH, EQUAL, column_width));
                }
                if (col < 2) row.push_back(text(" | ") | color(TextColors::border()));
            }
            dialog_content.push_back(hbox(row));
        }

        dialog_content.push_back(separator() | color(TextColors::border()));
        dialog_content.push_back(
            hbox({
                text("Press ") | color(TextColors::subtext()),
                text(key_bindings.get_key(KeyBindings::Action::ShowHelp)) | bold | color(TextColors::primary()),
                text(" to close") | color(TextColors::subtext()),
            }) | center
        );

        return vbox(dialog_content) | borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) | clear_under | center;
    });
}

bool ArxivApp::HandleHelpEvent(ftxui::Event event) {
    if(key_bindings.matches(event, KeyBindings::Action::ShowHelp) || event == Event::Escape) {
        show_help = false;
        return true;
    }
    return true; // block all other events while help is shown
}
