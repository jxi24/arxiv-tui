// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Utf8.hh"
#include "Arxiv/Views/Colors.hh"

#include <fmt/format.h>

#include <chrono>
#include <cmath>
#include <ctime>

#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

std::string ArxivApp::EmptyViewMessage() const {
    if (core.IsFetching())
        return "Fetching new articles…";
    auto key = [&](KeyBindings::Action a) {
        return KeyBindings::DisplayKey(key_bindings.get_key(a));
    };
    switch (core.GetFilterView()) {
    case AppCore::FilterView::Bookmarks:
        return "No bookmarks yet — press " + key(KeyBindings::Action::Bookmark) +
               " on an article to bookmark it";
    case AppCore::FilterView::Search:
        return "No matches for \"" + core.GetSearchQuery() + "\" — press " +
               key(KeyBindings::Action::Search) + " to search again";
    case AppCore::FilterView::Unread:
        return "All caught up — nothing unread";
    case AppCore::FilterView::NewArticles:
        return "No new articles since your last session";
    case AppCore::FilterView::Updated:
        return "No updated papers since your last session";
    case AppCore::FilterView::Recommended:
        return "No recommendations yet — rate articles with " +
               key(KeyBindings::Action::RateArticle) + " to train the ranker";
    case AppCore::FilterView::FollowedAuthors:
        return "No articles from followed authors";
    case AppCore::FilterView::Range:
        return "No articles in this date range — press " + key(KeyBindings::Action::SetDateRange) +
               " to change it";
    case AppCore::FilterView::Project:
        return "This project is empty — press " + key(KeyBindings::Action::CreateProject) +
               " on an article to add it";
    case AppCore::FilterView::Tag:
        return "No articles with this tag";
    default:
        return "No articles here — try another view";
    }
}

void ArxivApp::SelectArticle(int index) {
    core.SetArticleIndex(index); // clamps
    const int idx = core.GetArticleIndex();
    article_menu_index = idx;
    if (m_recorder)
        m_recorder->RecordSetArticleIndex(idx);
    title_start_position = 0;
    // Reading with the detail pane open counts as reading the article.
    const auto& articles = core.GetCurrentArticles();
    if (show_detail && !articles.empty())
        core.MarkArticleRead(articles[static_cast<size_t>(idx)].link);
}

namespace {
constexpr int kStatusWidth = 2;
} // namespace

void ArxivApp::SetupArticlePane() {
    MenuOption option;
    option.on_change = [&] { SelectArticle(article_menu_index); };
    article_list =
        Menu(&core.GetCurrentTitles(), &article_menu_index, option) | vscroll_indicator | frame |
        CatchEvent([&](Event event) {
            article_menu_index = core.GetArticleIndex(); // may have changed elsewhere
            if (key_bindings.matches(event, KeyBindings::Action::Next)) {
                SelectArticle(core.GetArticleIndex() + 1);
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::Previous)) {
                SelectArticle(std::max(core.GetArticleIndex() - 1, 0));
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::Bookmark)) {
                const auto& articles = core.GetCurrentArticles();
                if (!articles.empty()) {
                    if (core.GetSelectionCount() > 0) {
                        // Bulk bookmark: set all selected to bookmarked.
                        core.BookmarkSelected(true);
                    } else {
                        const std::string& link =
                            articles[static_cast<size_t>(core.GetArticleIndex())].link;
                        core.ToggleBookmark(link);
                        if (m_recorder)
                            m_recorder->RecordToggleBookmark(link);
                    }
                }
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::CreateProject)) {
                dialog_depth = Dialog::AssignProject;
                auto projects = core.GetProjects();
                const auto& articles = core.GetCurrentArticles();

                selected_projects.clear();
                checkbox_states.clear();
                if (core.GetSelectionCount() > 0) {
                    // Bulk mode: checkboxes start unchecked; confirm adds all selected.
                    for (const auto& project : projects)
                        checkbox_states[project] = false;
                } else if (!articles.empty()) {
                    // One call: begin() and end() must come from the same vector.
                    const auto linked = core.GetProjectsForArticle(
                        articles[static_cast<size_t>(core.GetArticleIndex())].link);
                    selected_projects = std::set<std::string>(linked.begin(), linked.end());
                    for (const auto& project : projects)
                        checkbox_states[project] = selected_projects.count(project) > 0;
                }
                selected_project_index = 0;
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::DeleteArticle)) {
                if (!core.GetCurrentArticles().empty()) {
                    dialog_depth = Dialog::ConfirmDelete;
                }
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::UndoDelete)) {
                if (core.CanUndo()) {
                    core.UndoLastDelete();
                }
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::DownloadArticle)) {
                if (core.GetCurrentArticles().empty())
                    return true;
                // Downloads run in the background; the status badge reports
                // progress and the outcome, and marks the article read.
                const auto& article =
                    core.GetCurrentArticles()[static_cast<size_t>(core.GetArticleIndex())];
                spdlog::debug("[App]: Downloading article ({})", article.id());
                if (m_recorder)
                    m_recorder->RecordDownloadArticle(article.id());
                StartTaskOrExplain(core.StartDownloadArticle(article.link), false);
                return true;
            }
            return false;
        });

    article_pane = Renderer(article_list, [&] {
        const auto& articles = core.GetCurrentArticles();
        if (articles.empty()) {
            auto header = focused_pane == 1
                              ? text(" Articles ") | bold | color(TextColors::base()) |
                                    bgcolor(TextColors::primary())
                              : text(" Articles ") | bold | color(TextColors::primary());
            return vbox({
                       header,
                       separator() | color(TextColors::border()),
                       text(EmptyViewMessage()) | center | color(TextColors::subtext()),
                   }) |
                   borderStyled(ROUNDED, TextColors::border());
        }

        // The pane's own size last frame; before the first frame, estimate
        // it from the screen.
        int articles_width = m_article_box.x_max - m_article_box.x_min + 1;
        int pane_height = m_article_box.y_max - m_article_box.y_min + 1;
        if (articles_width <= 1 || pane_height <= 1) {
            const int remaining = ScreenWidth() - FilterPaneWidth();
            articles_width = show_detail ? remaining / 2 : remaining;
            pane_height = ScreenHeight() - 1; // footer
        }
        // Border (2), header, separator, column header, separator.
        visible_rows = std::max(1, pane_height - 6);

        UpdateVisibleRange();

        bool show_scores =
            (core.GetFilterView() == AppCore::FilterView::Recommended) && core.IsRankerTrained();

        // Fixed widths for non-title columns (chars).
        // title gets the remaining space.
        auto col_width = [](const std::string& col) -> int {
            if (col == "date")
                return 10;
            if (col == "authors")
                return 24;
            if (col == "category")
                return 8;
            if (col == "score")
                return 8;
            if (col == "id")
                return 12;
            return 0; // title: computed below
        };

        // Compute title column width.
        // Usable area = articles_width - 2 (ROUNDED border) - 1 (vscroll_indicator).
        // Separators between adjacent columns = cols.size() - 1.
        // The title matters most: optional columns are dropped from the right
        // until it gets a readable share of the width.
        std::vector<std::string> configured_cols = core.GetConfig().get_article_columns();
        const bool has_title = std::find(configured_cols.begin(), configured_cols.end(), "title") !=
                               configured_cols.end();
        // 2 border + 1 scroll indicator + the 2-cell status column.
        const int usable = articles_width - 3 - kStatusWidth;
        const int min_title = std::max(24, usable * 45 / 100);
        auto title_width = [&] {
            int fixed = 0;
            for (const auto& col : configured_cols)
                fixed += col == "title" ? 0 : col_width(col);
            int seps = std::max(0, static_cast<int>(configured_cols.size()) - 1);
            return std::max(8, usable - fixed - seps);
        };
        int title_w = title_width();
        while (has_title && title_w < min_title && configured_cols.size() > 1) {
            auto last = std::find_if(configured_cols.rbegin(),
                                     configured_cols.rend(),
                                     [](const std::string& c) { return c != "title"; });
            if (last == configured_cols.rend())
                break;
            configured_cols.erase(std::next(last).base());
            title_w = title_width();
        }

        // Format helpers.
        auto fmt_date = [](const Article& a) -> std::string {
            auto t = std::chrono::system_clock::to_time_t(a.date);
            std::tm tm_val{};
            gmtime_r(&t, &tm_val); // the arXiv day, not the local one
            char buf[16];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_val);
            return buf;
        };
        // By character, not byte: accented names must not be cut mid-letter.
        auto truncate = [](const std::string& s, int w) { return Utf8::Truncate(s, w); };

        // Build column header row (blank over the status column).
        Elements header_cells = {text(std::string(kStatusWidth, ' ')) |
                                 size(WIDTH, EQUAL, kStatusWidth)};
        for (size_t ci = 0; ci < configured_cols.size(); ++ci) {
            const auto& col = configured_cols[ci];
            int w = (col == "title") ? title_w : col_width(col);
            std::string label = col;
            label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
            header_cells.push_back(text(truncate(label, w)) | bold | color(TextColors::subtext()) |
                                   size(WIDTH, EQUAL, w));
            if (ci + 1 < configured_cols.size())
                header_cells.push_back(text(" ") | color(TextColors::border()));
        }
        Element col_header = hbox(header_cells);

        Elements menu_items;
        for (size_t i = static_cast<size_t>(top_article_index);
             i < std::min(static_cast<size_t>(top_article_index + visible_rows), articles.size());
             ++i) {
            const auto& article = articles[i];
            bool is_current = (i == static_cast<size_t>(core.GetArticleIndex()));
            bool is_selected = core.IsSelected(article.link);

            // Compute score badge if needed.
            std::string score_str;
            if (show_scores) {
                float score = core.GetPredictedScore(article);
                char buf[16];
                std::snprintf(buf, sizeof(buf), "[%.1f★]", static_cast<double>(score));
                score_str = buf;
            }

            // Compute the title cell value (with prefix and scrolling for current row).
            std::string title_cell;
            if (has_title) {
                std::string title = article.title.empty() ? "(no title)" : article.title;
                if (is_current) {
                    // Scroll a title too long for the column, a character at
                    // a time (a byte offset could start mid-letter).
                    if (Utf8::CharStarts(title).size() > static_cast<size_t>(title_w - 2)) {
                        title = Utf8::Rotate(
                            title, static_cast<size_t>(std::floor(title_start_position)), "    ");
                    }
                    title_cell = "> " + title;
                } else {
                    title_cell = "  " + title;
                }
            }

            // Build the row as an hbox of column cells, after a status column:
            // "+" selected, "★" bookmarked (single-width, so titles line up).
            Elements cells = {
                hbox({text(is_selected ? "+" : " ") | color(TextColors::secondary()),
                      text(article.bookmarked ? "★" : " ") | color(TextColors::secondary())}) |
                size(WIDTH, EQUAL, kStatusWidth)};
            for (size_t ci = 0; ci < configured_cols.size(); ++ci) {
                const auto& col = configured_cols[ci];
                int w = (col == "title") ? title_w : col_width(col);
                std::string val;
                if (col == "title") {
                    val = title_cell;
                } else if (col == "authors") {
                    val = truncate(article.authors, w);
                } else if (col == "date") {
                    val = fmt_date(article);
                } else if (col == "category") {
                    val = truncate(article.category, w);
                } else if (col == "score") {
                    val = score_str.empty() ? "" : truncate(score_str, w);
                } else if (col == "id") {
                    val = truncate(article.id(), w);
                }
                cells.push_back(text(val) | size(WIDTH, EQUAL, w));
                if (ci + 1 < configured_cols.size())
                    cells.push_back(text(" ") | color(TextColors::border()));
            }

            Element row = hbox(cells);
            if (is_current) {
                row = row | (focused_pane == 1 ? (bold | color(TextColors::primary()))
                                               : color(TextColors::subtext()));
            } else {
                row = row | color(article.read ? TextColors::subtext() : TextColors::text());
            }
            menu_items.push_back(row);
        }

        int pending = core.PendingRatings();
        std::size_t sel_count = core.GetSelectionCount();
        auto header_text = focused_pane == 1
                               ? text(" Articles ") | bold | color(TextColors::base()) |
                                     bgcolor(TextColors::primary())
                               : text(" Articles ") | bold | color(TextColors::primary());
        Element header = hbox({
            header_text,
            sel_count > 0
                ? text("  [" + std::to_string(sel_count) + " selected]") |
                      color(TextColors::secondary())
                : (core.IsTraining() ? text("  [Training…]") | color(TextColors::secondary())
                                     : (pending > 0 ? text("  [" + std::to_string(pending) +
                                                           " rating(s) pending]") |
                                                          color(TextColors::subtext())
                                                    : emptyElement())),
        });
        return vbox({header,
                     separator() | color(TextColors::border()),
                     col_header,
                     separator() | color(TextColors::border()),
                     vbox(menu_items) | vscroll_indicator | frame}) |
               borderStyled(ROUNDED, TextColors::border()) | reflect(m_article_box);
    });
}
