// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/AuthorMatch.hh"
#include "Arxiv/Utf8.hh"
#include "Arxiv/Views/Colors.hh"

#include <algorithm>

#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

namespace {

std::vector<std::string> split_authors(const std::string& authors) {
    std::vector<std::string> names;
    std::size_t start = 0;
    while (start <= authors.size()) {
        auto comma = authors.find(',', start);
        std::string name =
            authors.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        auto b = name.find_first_not_of(" \t");
        auto e = name.find_last_not_of(" \t");
        if (b != std::string::npos)
            names.push_back(name.substr(b, e - b + 1));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return names;
}

const std::string kNoInitial = "no first initial — matches every author with this last name";

} // namespace

// Rows of the dialog: the focused article's authors, then every followed name.
std::vector<ArxivApp::FollowRow> ArxivApp::FollowRows() const {
    const auto followed = core.GetFollowedAuthors();
    auto is_followed = [&](const std::string& n) {
        return std::find(followed.begin(), followed.end(), n) != followed.end();
    };
    std::vector<FollowRow> rows;
    const auto& articles = core.GetCurrentArticles();
    const int idx = core.GetArticleIndex();
    if (idx >= 0 && idx < static_cast<int>(articles.size()))
        for (const auto& n : split_authors(articles[static_cast<std::size_t>(idx)].authors))
            rows.push_back({n, true, is_followed(n)});
    for (const auto& n : followed)
        rows.push_back({n, false, true});
    return rows;
}

// A name without a first initial matches everyone with that last name, so it
// needs a second Enter to confirm.
void ArxivApp::RequestFollow(const std::string& name) {
    if (!HasFirstInitial(name)) {
        follow_pending_name = name;
        return;
    }
    core.FollowAuthor(name);
    spdlog::info("follow_author name={}", name);
}

void ArxivApp::SetupFollowAuthorsDialog() {
    follow_dialog = Renderer([&] {
        if (dialog_depth != Dialog::FollowAuthors)
            return emptyElement();

        const auto rows = FollowRows();
        const auto followed = core.GetFollowedAuthors();
        Elements lines;
        lines.push_back(text("Followed Authors") | bold | center | color(TextColors::primary()));
        lines.push_back(separator() | color(TextColors::border()));

        bool article_header = false, following_header = false;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            const auto& row = rows[static_cast<std::size_t>(i)];
            if (row.from_article && !article_header) {
                lines.push_back(text("This article") | bold | color(TextColors::subtext()));
                article_header = true;
            }
            if (!row.from_article && !following_header) {
                lines.push_back(text("Following") | bold | color(TextColors::subtext()));
                following_header = true;
            }
            std::string label = row.from_article ? (row.followed ? "[x] " : "[ ] ") : "    ";
            label += row.name;
            if (row.from_article && !row.followed) {
                // Already covered by another followed entry ("W. Li" for "Wei Li")?
                for (const auto& f : followed)
                    if (AuthorMatches(f, row.name)) {
                        label += "  (followed as " + f + ")";
                        break;
                    }
            }
            Elements cells = {text("  " + label)};
            if (!row.from_article && !HasFirstInitial(row.name))
                cells.push_back(text("  ⚠ " + kNoInitial) | color(TextColors::error()));
            auto line = hbox(cells);
            if (i == follow_selected_index && !follow_adding && follow_pending_name.empty())
                line = line | bold | color(TextColors::base()) | bgcolor(TextColors::primary());
            else
                line = line | color(TextColors::text());
            lines.push_back(line);
        }
        if (!following_header)
            lines.push_back(text("  (not following anyone yet)") | color(TextColors::subtext()));

        lines.push_back(separator() | color(TextColors::border()));
        if (!follow_pending_name.empty()) {
            lines.push_back(paragraph("⚠ \"" + follow_pending_name + "\" has " + kNoInitial +
                                      ". Add a first initial (e.g. \"W. Li\") to pick out one "
                                      "person.") |
                            color(TextColors::error()));
            lines.push_back(text("Enter: follow anyway   Esc: cancel") |
                            color(TextColors::subtext()));
        } else if (follow_adding) {
            lines.push_back(text("Add: " + follow_new_name + "_") | color(TextColors::text()));
            lines.push_back(
                text("e.g. \"W. Li\" or \"J. A. Smith\"   Enter: follow   Esc: cancel") |
                color(TextColors::subtext()));
        } else {
            lines.push_back(text("j/k: move  Enter: follow/unfollow  d: unfollow  +: add a name  "
                                 "Esc: close") |
                            color(TextColors::subtext()));
        }
        return vbox(lines) | size(WIDTH, LESS_THAN, 90) |
               borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) |
               clear_under | center;
    });
}

bool ArxivApp::HandleFollowAuthorsEvent(ftxui::Event event) {
    if (!follow_pending_name.empty()) {
        if (event == Event::Return) {
            core.FollowAuthor(follow_pending_name);
            spdlog::info("follow_author name={} (no first initial)", follow_pending_name);
            follow_pending_name.clear();
        } else if (event == Event::Escape) {
            follow_pending_name.clear();
        }
        return true;
    }

    if (follow_adding) {
        if (event == Event::Return) {
            follow_adding = false;
            if (!follow_new_name.empty())
                RequestFollow(follow_new_name);
            follow_new_name.clear();
        } else if (event == Event::Escape) {
            follow_adding = false;
            follow_new_name.clear();
        } else if (event == Event::Backspace) {
            if (!follow_new_name.empty())
                Utf8::PopBack(follow_new_name);
        } else if (event.is_character()) {
            follow_new_name += event.character();
        }
        return true;
    }

    const auto rows = FollowRows();
    const int last = static_cast<int>(rows.size()) - 1;
    if (key_bindings.matches(event, KeyBindings::Action::Next) || event == Event::ArrowDown) {
        follow_selected_index = std::min(follow_selected_index + 1, std::max(0, last));
        return true;
    }
    if (key_bindings.matches(event, KeyBindings::Action::Previous) || event == Event::ArrowUp) {
        follow_selected_index = std::max(0, follow_selected_index - 1);
        return true;
    }
    if (event == Event::Character("+")) {
        follow_adding = true;
        return true;
    }
    if (event == Event::Escape) {
        dialog_depth = Dialog::None;
        return true;
    }
    if (follow_selected_index < 0 || follow_selected_index > last)
        return true;
    const auto& row = rows[static_cast<std::size_t>(follow_selected_index)];
    if (event == Event::Return) {
        if (row.followed)
            core.UnfollowAuthor(row.name);
        else
            RequestFollow(row.name);
    } else if ((event == Event::Character("d") || event == Event::Delete) && row.followed) {
        core.UnfollowAuthor(row.name);
    }
    // Rows can disappear (an unfollowed entry): keep the cursor in range.
    follow_selected_index =
        std::max(0, std::min(follow_selected_index, static_cast<int>(FollowRows().size()) - 1));
    return true;
}
