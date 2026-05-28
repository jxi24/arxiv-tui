// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

#include <fmt/format.h>

#include <cmath>

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
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::Previous)) {
                int idx = std::max(core.GetArticleIndex() - 1, 0);
                core.SetArticleIndex(idx);
                if (m_recorder)
                    m_recorder->RecordSetArticleIndex(idx);
                title_start_position = 0;
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::Bookmark)) {
                auto articles = core.GetCurrentArticles();
                if (!articles.empty()) {
                    const std::string& link =
                        articles[static_cast<size_t>(core.GetArticleIndex())].link;
                    core.ToggleBookmark(link);
                    if (m_recorder)
                        m_recorder->RecordToggleBookmark(link);
                }
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::CreateProject)) {
                dialog_depth = Dialog::AssignProject;
                auto projects = core.GetProjects();
                auto articles = core.GetCurrentArticles();

                selected_projects.clear();
                checkbox_states.clear();
                if (!articles.empty()) {
                    selected_projects = std::set<std::string>(
                        core.GetProjectsForArticle(
                                articles[static_cast<size_t>(core.GetArticleIndex())].link)
                            .begin(),
                        core.GetProjectsForArticle(
                                articles[static_cast<size_t>(core.GetArticleIndex())].link)
                            .end());
                }

                for (const auto& project : projects) {
                    checkbox_states[project] = selected_projects.count(project) > 0;
                }
                selected_project_index = 0;
                return true;
            }
            if (key_bindings.matches(event, KeyBindings::Action::DownloadArticle)) {
                auto article =
                    core.GetCurrentArticles()[static_cast<size_t>(core.GetArticleIndex())];
                spdlog::debug("[App]: Downloading article ({}) to articles folder", article.id());
                if (m_recorder)
                    m_recorder->RecordDownloadArticle(article.id());
                bool success = core.DownloadArticle(article.id());
                if (!success) {
                    dialog_depth = Dialog::Error;
                    err_msg = fmt::format("Failed to download article: {}", article.id());
                }
                return true;
            }
            return false;
        });

    article_pane = Renderer(article_list, [&] {
        auto articles = core.GetCurrentArticles();
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
        Elements menu_items;
        for (size_t i = static_cast<size_t>(top_article_index);
             i < std::min(static_cast<size_t>(top_article_index + visible_rows), articles.size());
             ++i) {
            const auto& article = articles[i];
            std::string title = article.title;

            if (core.IsSelected(article.link)) {
                title = "[*] " + title;
            } else if (article.bookmarked) {
                title = "⭐ " + title;
            }

            std::string score_badge;
            if (show_scores) {
                float score = core.GetPredictedScore(article);
                char buf[16];
                std::snprintf(buf, sizeof(buf), " [%.1f★]", static_cast<double>(score));
                score_badge = buf;
            }

            if (i == static_cast<size_t>(core.GetArticleIndex())) {
                size_t start_pos =
                    static_cast<size_t>(std::floor(title_start_position)) % article.title.length();
                if (title.length() > static_cast<size_t>(articles_width - 2)) {
                    title = title.substr(start_pos) + "    " + title.substr(0, start_pos);
                }
                title = "> " + title + score_badge;
                if (focused_pane == 1) {
                    menu_items.push_back(text(title) | bold | color(TextColors::primary()));
                } else {
                    menu_items.push_back(text(title) | color(TextColors::subtext()));
                }
            } else {
                title = "  " + title + score_badge;
                menu_items.push_back(text(title) | color(TextColors::text()));
            }
        }

        int pending = core.PendingRatings();
        auto header_text = focused_pane == 1
                               ? text(" Articles ") | bold | color(TextColors::base()) |
                                     bgcolor(TextColors::primary())
                               : text(" Articles ") | bold | color(TextColors::primary());
        Element header = hbox({
            header_text,
            core.IsTraining()
                ? text("  [Training…]") | color(TextColors::secondary())
                : (pending > 0 ? text("  [" + std::to_string(pending) + " rating(s) pending]") |
                                     color(TextColors::subtext())
                               : emptyElement()),
        });
        return vbox({header,
                     separator() | color(TextColors::border()),
                     vbox(menu_items) | vscroll_indicator | frame}) |
               borderStyled(ROUNDED, TextColors::border());
    });
}
