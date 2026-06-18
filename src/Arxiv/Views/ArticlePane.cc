// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

#include <fmt/format.h>

#include <chrono>
#include <cmath>
#include <ctime>

#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupArticlePane() {
    article_list =
        Menu(&core.GetCurrentTitles(), &core.GetArticleIndex()) | vscroll_indicator | frame |
        CatchEvent([&](Event event) {
            if (key_bindings.matches(event, KeyBindings::Action::Next)) {
                int idx = std::min(core.GetArticleIndex() + 1,
                                   static_cast<int>(core.GetCurrentArticles().size()) - 1);
                core.SetArticleIndex(idx);
                if (m_recorder)
                    m_recorder->RecordSetArticleIndex(idx);
                title_start_position = 0;
                if (show_detail) {
                    const auto& articles = core.GetCurrentArticles();
                    if (!articles.empty())
                        core.MarkArticleRead(articles[static_cast<size_t>(idx)].link);
                }
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::Previous)) {
                int idx = std::max(core.GetArticleIndex() - 1, 0);
                core.SetArticleIndex(idx);
                if (m_recorder)
                    m_recorder->RecordSetArticleIndex(idx);
                title_start_position = 0;
                if (show_detail) {
                    const auto& articles = core.GetCurrentArticles();
                    if (!articles.empty())
                        core.MarkArticleRead(articles[static_cast<size_t>(idx)].link);
                }
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
                    selected_projects = std::set<std::string>(
                        core.GetProjectsForArticle(
                                articles[static_cast<size_t>(core.GetArticleIndex())].link)
                            .begin(),
                        core.GetProjectsForArticle(
                                articles[static_cast<size_t>(core.GetArticleIndex())].link)
                            .end());
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
                auto article =
                    core.GetCurrentArticles()[static_cast<size_t>(core.GetArticleIndex())];
                spdlog::debug("[App]: Downloading article ({}) to articles folder", article.id());
                if (m_recorder)
                    m_recorder->RecordDownloadArticle(article.id());
                bool success = core.DownloadArticle(article.id());
                if (success) {
                    core.MarkArticleRead(article.link);
                } else {
                    dialog_depth = Dialog::Error;
                    err_msg = fmt::format("Failed to download article: {}", article.id());
                }
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
                       text("No articles available") | center | color(TextColors::subtext()),
                       separator() | color(TextColors::border()),
                       text("Try changing filters.") | center | color(TextColors::subtext()),
                   }) |
                   borderStyled(ROUNDED, TextColors::border());
        }

        int filter_width = FilterPaneWidth();
        int remaining_width = Terminal::Size().dimx - filter_width - border_size;
        int articles_width = show_detail ? remaining_width / 2 : remaining_width;
        visible_rows = Terminal::Size().dimy - 5;

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

        const auto& configured_cols = core.GetConfig().get_article_columns();

        // Compute title column width.
        // Usable area = articles_width - 2 (ROUNDED border) - 1 (vscroll_indicator).
        // Separators between adjacent columns = configured_cols.size() - 1.
        bool has_title = false;
        int fixed_cols_w = 0;
        for (const auto& col : configured_cols) {
            if (col == "title") {
                has_title = true;
            } else {
                fixed_cols_w += col_width(col);
            }
        }
        int num_seps = std::max(0, static_cast<int>(configured_cols.size()) - 1);
        int usable = articles_width - 3; // 2 border + 1 scroll indicator
        int title_w = std::max(8, usable - fixed_cols_w - num_seps);

        // Format helpers.
        auto fmt_date = [](const Article& a) -> std::string {
            auto t = std::chrono::system_clock::to_time_t(a.date);
            std::tm tm_val{};
            localtime_r(&t, &tm_val);
            char buf[16];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_val);
            return buf;
        };
        auto truncate = [](const std::string& s, int w) -> std::string {
            if (w <= 0)
                return "";
            if (static_cast<int>(s.size()) <= w)
                return s;
            return s.substr(0, static_cast<size_t>(w - 1)) + "…";
        };

        // Build column header row.
        Elements header_cells;
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
                std::string title = article.title;
                if (is_selected) {
                    title = "[*] " + title;
                } else if (article.bookmarked) {
                    title = "⭐ " + title;
                }
                if (is_current) {
                    size_t start_pos = static_cast<size_t>(std::floor(title_start_position)) %
                                       article.title.length();
                    if (title.length() > static_cast<size_t>(title_w - 2)) {
                        title = title.substr(start_pos) + "    " + title.substr(0, start_pos);
                    }
                    title_cell = "> " + title;
                } else {
                    title_cell = "  " + title;
                }
            }

            // Build the row as an hbox of column cells.
            Elements cells;
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
               borderStyled(ROUNDED, TextColors::border());
    });
}
