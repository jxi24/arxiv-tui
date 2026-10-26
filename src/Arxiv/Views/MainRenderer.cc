// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Clipboard.hh"
#include "Arxiv/Process.hh"
#include "Arxiv/Views/Colors.hh"

#include <chrono>
#include <cstdio>
#include <ctime>

#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupMainRenderer() {
    main_renderer = Renderer(main_container, [&] {
        int filter_width = FilterPaneWidth();
        int remaining_width = Terminal::Size().dimx - filter_width - border_size;

        int articles_width = show_detail ? remaining_width / 2 : remaining_width;
        int detail_width = show_detail ? remaining_width / 2 : 0;

        std::vector<Element> panes = {
            filter_pane->Render() | size(WIDTH, EQUAL, filter_width),
            article_pane->Render() | size(WIDTH, EQUAL, articles_width),
        };
        if (show_detail) {
            panes.push_back(detail_view->Render() | size(WIDTH, EQUAL, detail_width));
        }
        Element body = hbox(std::move(panes)) | bgcolor(TextColors::base());

        auto fmt_key = [](std::string k) { return k == " " ? std::string("<space>") : k; };
        std::string quit_key = fmt_key(key_bindings.get_key(KeyBindings::Action::Quit));
        std::string help_key = fmt_key(key_bindings.get_key(KeyBindings::Action::ShowHelp));

        // Active filter label + context
        auto filter_view = core.GetFilterView();
        std::string filter_label;
        std::string filter_detail;
        switch (filter_view) {
        case AppCore::FilterView::All:
            filter_label = "All";
            break;
        case AppCore::FilterView::Bookmarks:
            filter_label = "Bookmarks";
            break;
        case AppCore::FilterView::Today:
            filter_label = "Today";
            break;
        case AppCore::FilterView::Range: {
            filter_label = "Date Range";
            auto [s, e] = core.GetDateRange();
            if (!s.empty() || !e.empty())
                filter_detail = s + " → " + e;
            break;
        }
        case AppCore::FilterView::Search:
            filter_label = "Search";
            filter_detail = "\"" + core.GetSearchQuery() + "\"";
            break;
        case AppCore::FilterView::Recommended:
            filter_label = "Recommended";
            break;
        case AppCore::FilterView::FollowedAuthors:
            filter_label = "Followed";
            break;
        case AppCore::FilterView::NewArticles:
            filter_label = "New Articles";
            break;
        case AppCore::FilterView::Updated:
            filter_label = "Updated";
            break;
        case AppCore::FilterView::Unread:
            filter_label = "Unread";
            break;
        case AppCore::FilterView::Tag:
            filter_label = "#" + core.CurrentTag();
            break;
        case AppCore::FilterView::Project:
            filter_label = core.CurrentProject();
            break;
        }

        // Category filter suffix (when not all categories are active)
        const auto& active_cats = core.GetActiveCategories();
        const auto& all_topics = core.GetTopics();
        const std::set<std::string> all_topics_set(all_topics.begin(), all_topics.end());
        if (active_cats != all_topics_set && !all_topics.empty()) {
            std::string cats;
            for (const auto& c : active_cats) {
                if (!cats.empty())
                    cats += ",";
                cats += c;
            }
            filter_detail = (filter_detail.empty() ? "" : filter_detail + "  ") +
                            "cats:" + (cats.empty() ? "none" : cats);
        }

        // Article position
        const auto& articles = core.GetCurrentArticles();
        int total = static_cast<int>(articles.size());
        int current = total > 0 ? core.GetArticleIndex() + 1 : 0;
        std::string position = std::to_string(current) + " / " + std::to_string(total);

        // Topics list (comma-separated, right-aligned)
        const auto& topics = m_config.get_topics();
        std::string topics_str;
        for (size_t i = 0; i < topics.size(); ++i) {
            if (i > 0)
                topics_str += ", ";
            topics_str += topics[i];
        }

        Elements footer_els = {
            text(" "),
            text(quit_key) | bold | color(TextColors::primary()),
            text(" quit  ") | color(TextColors::subtext()),
            text(help_key) | bold | color(TextColors::primary()),
            text(" keybindings ") | color(TextColors::subtext()),
            text("│") | color(TextColors::border()),
            text(" filter: ") | color(TextColors::subtext()),
            text(filter_label) | bold | color(TextColors::primary()),
        };
        if (!filter_detail.empty()) {
            footer_els.push_back(text("  ") | color(TextColors::subtext()));
            footer_els.push_back(text(filter_detail) | color(TextColors::text()));
        }
        footer_els.push_back(text("  ") | color(TextColors::subtext()));
        footer_els.push_back(text(position) | color(TextColors::text()));
        footer_els.push_back(filler());
        // The topic list is the least important part: leave it out rather
        // than let it run into the position counter on a narrow terminal.
        const std::size_t left_width = quit_key.size() + help_key.size() + filter_label.size() +
                                       (filter_detail.empty() ? 0 : filter_detail.size() + 2) +
                                       position.size() + 34;
        if (left_width + topics_str.size() + 2 <= static_cast<std::size_t>(Terminal::Size().dimx))
            footer_els.push_back(text(topics_str) | color(TextColors::subtext()));
        footer_els.push_back(text(" "));

        auto footer =
            hbox(std::move(footer_els)) | bgcolor(TextColors::surface()) | size(HEIGHT, EQUAL, 1);

        Element document = vbox({body | flex, footer});

        if (dialog_depth == Dialog::NewProject) {
            document = dbox({document, RenderNewProjectDialog()});
        } else if (dialog_depth == Dialog::AssignProject) {
            document = dbox({document, project_dialog->Render()});
        } else if (dialog_depth == Dialog::Error) {
            auto error_dialog = vbox({
                                    text("ERROR") | bold | center | color(TextColors::error()),
                                    separator() | color(TextColors::error()),
                                    text(err_msg) | color(TextColors::text()),
                                }) |
                                borderStyled(ROUNDED, TextColors::error()) |
                                bgcolor(TextColors::surface()) | clear_under | center;
            document = dbox({document, error_dialog});
        } else if (dialog_depth == Dialog::ConfigWarning) {
            Elements lines = {
                text("Config problem") | bold | center | color(TextColors::error()),
                separator() | color(TextColors::error()),
            };
            for (const auto& w : m_config.get_warnings())
                lines.push_back(paragraph("• " + w) | color(TextColors::text()));
            lines.push_back(separator() | color(TextColors::border()));
            lines.push_back(
                text("Fix config.yml, or change these in Settings (" +
                     KeyBindings::DisplayKey(key_bindings.get_key(KeyBindings::Action::Settings)) +
                     ").") |
                color(TextColors::subtext()));
            lines.push_back(text("Press Enter or Esc to continue") | color(TextColors::subtext()));
            auto warning_dialog = vbox(lines) | size(WIDTH, LESS_THAN, 76) |
                                  borderStyled(ROUNDED, TextColors::error()) |
                                  bgcolor(TextColors::surface()) | clear_under | center;
            document = dbox({document, warning_dialog});
        } else if (dialog_depth == Dialog::Success) {
            auto success_dialog =
                vbox({
                    text("OK") | bold | center | color(TextColors::primary()),
                    separator() | color(TextColors::primary()),
                    text(success_msg) | color(TextColors::text()),
                    text("Press Enter or Esc to dismiss") | color(TextColors::subtext()),
                }) |
                borderStyled(ROUNDED, TextColors::primary()) | bgcolor(TextColors::surface()) |
                clear_under | center;
            document = dbox({document, success_dialog});
        } else if (dialog_depth == Dialog::DateRange) {
            document = dbox({document, date_range_dialog->Render()});
        } else if (dialog_depth == Dialog::Search) {
            document = dbox({document, search_dialog->Render()});
        } else if (dialog_depth == Dialog::Rating) {
            document = dbox({document, rating_dialog->Render()});
        } else if (dialog_depth == Dialog::Notes) {
            document = dbox({document, note_dialog->Render()});
        } else if (dialog_depth == Dialog::Export) {
            document = dbox({document, export_dialog->Render()});
        } else if (dialog_depth == Dialog::Import) {
            document = dbox({document, import_dialog->Render()});
        } else if (dialog_depth == Dialog::KeywordEditor) {
            document = dbox({document, keyword_dialog->Render()});
        } else if (dialog_depth == Dialog::FollowAuthors) {
            document = dbox({document, follow_dialog->Render()});
        } else if (dialog_depth == Dialog::Tags) {
            document = dbox({document, tags_dialog->Render()});
        } else if (dialog_depth == Dialog::CategoryFilter) {
            document = dbox({document, category_dialog->Render()});
        } else if (dialog_depth == Dialog::Settings) {
            document = dbox({document, settings_dialog->Render()});
        } else if (dialog_depth == Dialog::ConfirmDelete) {
            std::size_t n = core.GetSelectionCount();
            std::string msg = n > 0 ? "Delete " + std::to_string(n) + " selected article(s)?"
                                    : "Delete this article?";
            auto confirm_dialog = vbox({
                                      text("DELETE") | bold | center | color(TextColors::error()),
                                      separator() | color(TextColors::error()),
                                      text(msg) | center | color(TextColors::text()),
                                      separator() | color(TextColors::border()),
                                      hbox({
                                          text(" [y] Yes ") | bold | color(TextColors::error()),
                                          text("  "),
                                          text(" [n] No  ") | color(TextColors::subtext()),
                                      }) | center,
                                  }) |
                                  borderStyled(ROUNDED, TextColors::error()) |
                                  bgcolor(TextColors::surface()) | clear_under | center;
            document = dbox({document, confirm_dialog});
        }

        if (show_help) {
            document = dbox({document, help_dialog->Render()});
        }

        auto task = core.GetBackgroundTaskStatus();
        const bool show_task_result =
            !task.running && !task.label.empty() &&
            std::chrono::steady_clock::now() - task.finished_at < std::chrono::seconds(8);
        if (task.running || show_task_result) {
            Element badge;
            if (task.running) {
                std::string progress = task.label;
                if (task.total > 1)
                    progress += " " + std::to_string(task.done) + "/" + std::to_string(task.total);
                badge = text(" " + progress + "… ") | bold | color(TextColors::primary());
            } else if (!task.error.empty()) {
                badge = text(" ✗ " + task.error + " ") | bold | color(TextColors::error());
            } else {
                badge = text(" ✓ " + task.message + " ") | color(TextColors::primary());
            }
            auto overlay = vbox({
                filler(),
                hbox({filler(), badge | bgcolor(TextColors::surface())}),
                text(""), // stay clear of the fetching badge row
            });
            document = dbox({document, overlay});
        }

        if (core.IsFetching()) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
            const char* dots[] = {"   ", ".  ", ".. ", "..."};
            const char* anim = dots[(ms / 400) % 4];
            auto badge = hbox({
                             text(" Fetching new articles") | color(TextColors::primary()) | bold,
                             text(anim) | color(TextColors::primary()),
                             text(" "),
                         }) |
                         bgcolor(TextColors::surface());

            auto overlay = vbox({
                filler(),
                hbox({filler(), badge}),
            });
            document = dbox({document, overlay});
        }

        return document;
    });
}

void ArxivApp::SetupEventHandler() {
    event_handler = CatchEvent(main_renderer, [&](Event event) {
        // Help overlay takes priority: block or close
        if (show_help)
            return HandleHelpEvent(event);

        // Help toggle (open help from any state)
        if (key_bindings.matches(event, KeyBindings::Action::ShowHelp)) {
            ToggleHelp();
            return true;
        }

        // DateRange dialog: handle events, or open via key binding
        if (dialog_depth == Dialog::DateRange)
            return HandleDateRangeEvent(event);
        if (key_bindings.matches(event, KeyBindings::Action::SetDateRange) &&
            core.GetFilterView() == AppCore::FilterView::Range) {
            dialog_depth = Dialog::DateRange;
            date_input_mode = DateInputMode::Start;
            start_date.clear();
            end_date.clear();
            return true;
        }

        // Search dialog: handle events, or open via key binding
        if (dialog_depth == Dialog::Search)
            return HandleSearchEvent(event);
        if (key_bindings.matches(event, KeyBindings::Action::Search)) {
            dialog_depth = Dialog::Search;
            search_query.clear();
            search_area_selection = false;
            search_area_index = static_cast<int>(search_field);
            return true;
        }

        if (dialog_depth == Dialog::Rating)
            return HandleRatingEvent(event);
        if (dialog_depth == Dialog::Notes)
            return HandleNotesEvent(event);
        if (dialog_depth == Dialog::Export)
            return HandleExportEvent(event);

        if (dialog_depth == Dialog::ConfigWarning) {
            if (event == Event::Return || event == Event::Escape)
                dialog_depth = Dialog::None;
            return true;
        }

        // Success dialog dismiss
        if (dialog_depth == Dialog::Success) {
            if (event == Event::Return || event == Event::Escape) {
                dialog_depth = Dialog::None;
                success_msg = "";
            }
            return true;
        }

        // Delete confirmation: y = confirm, anything else = cancel
        if (dialog_depth == Dialog::ConfirmDelete) {
            if (event == Event::Character('y') || event == Event::Character('Y')) {
                core.DeleteCurrentOrSelected();
                dialog_depth = Dialog::None;
            } else {
                dialog_depth = Dialog::None;
            }
            return true;
        }

        if (dialog_depth == Dialog::Import)
            return HandleImportEvent(event);
        if (dialog_depth == Dialog::CategoryFilter)
            return HandleCategoryFilterEvent(event);
        if (dialog_depth == Dialog::KeywordEditor)
            return HandleKeywordEditorEvent(event);
        if (dialog_depth == Dialog::FollowAuthors)
            return HandleFollowAuthorsEvent(event);
        if (dialog_depth == Dialog::Tags)
            return HandleTagsEvent(event);
        if (dialog_depth == Dialog::Settings)
            return HandleSettingsEvent(event);

        return HandleGlobalEvent(event);
    });
}

bool ArxivApp::HandleGlobalEvent(ftxui::Event event) {
    // Open notes editor (only when viewing a project with an article selected)
    if (key_bindings.matches(event, KeyBindings::Action::EditNote)) {
        if (core.GetFilterView() == AppCore::FilterView::Project) {
            const auto& articles = core.GetCurrentArticles();
            if (!articles.empty()) {
                note_project_name = core.CurrentProject();
                note_article_link = articles[static_cast<size_t>(core.GetArticleIndex())].link;
                note_edit_text = core.GetProjectNote(note_project_name, note_article_link);
                dialog_depth = Dialog::Notes;
            }
        }
        return true;
    }

    // BibTeX for current article: copy to clipboard, fall back to file
    if (key_bindings.matches(event, KeyBindings::Action::ExportBibTeX)) {
        const auto& articles = core.GetCurrentArticles();
        if (!articles.empty()) {
            const auto& art = articles[static_cast<size_t>(core.GetArticleIndex())];
            std::string bibtex = core.GetBibtex(art);
            std::string backend = Clipboard::DetectBackend(m_config.get_clipboard_backend());
            if (!backend.empty() && Clipboard::Copy(bibtex, backend)) {
                spdlog::info("bibtex_to_clipboard link={} backend={}", art.link, backend);
                success_msg = "BibTeX copied to clipboard";
                dialog_depth = Dialog::Success;
            } else {
                std::string path = core.ExportPath(art.file_stem() + ".bib");
                bool ok = core.ExportArticleBibTeX(art, path);
                if (m_recorder)
                    m_recorder->RecordExportArticleBibTeX(art.link, path);
                if (ok) {
                    success_msg = "BibTeX saved to " + path;
                    dialog_depth = Dialog::Success;
                } else {
                    err_msg = "BibTeX export failed: " + path;
                    dialog_depth = Dialog::Error;
                }
            }
        }
        return true;
    }

    // Open keyword editor
    if (key_bindings.matches(event, KeyBindings::Action::EditKeywords)) {
        keyword_edit_list = core.GetKeywords();
        keyword_new_entry.clear();
        keyword_selected_index = 0;
        dialog_depth = Dialog::KeywordEditor;
        return true;
    }

    // Open the followed-authors dialog (authors of the focused article + list)
    if (key_bindings.matches(event, KeyBindings::Action::FollowAuthors)) {
        follow_selected_index = 0;
        follow_adding = false;
        follow_new_name.clear();
        follow_pending_name.clear();
        dialog_depth = Dialog::FollowAuthors;
        return true;
    }

    // Open the tags dialog for the selection or the focused article
    if (key_bindings.matches(event, KeyBindings::Action::TagArticle)) {
        if (!core.GetCurrentArticles().empty()) {
            tag_selected_index = 0;
            tag_adding = false;
            tag_new_name.clear();
            tag_pending_delete.clear();
            dialog_depth = Dialog::Tags;
        }
        return true;
    }

    // Open arXiv category filter
    if (key_bindings.matches(event, KeyBindings::Action::FilterCategories)) {
        category_selected_index = 0;
        dialog_depth = Dialog::CategoryFilter;
        return true;
    }

    // Open settings dialog
    if (key_bindings.matches(event, KeyBindings::Action::Settings)) {
        settings_section = 0;
        settings_field_index = 0;
        settings_editing = false;
        settings_edit_buffer.clear();
        // m_config already holds the last saved settings; resetting it from
        // the core's startup copy used to discard them.
        m_settings_on_open = m_config;
        dialog_depth = Dialog::Settings;
        return true;
    }

    // Toggle selection of focused article (for Export Selected Digest)
    if (key_bindings.matches(event, KeyBindings::Action::ToggleSelection)) {
        const auto& articles = core.GetCurrentArticles();
        if (!articles.empty()) {
            int idx = core.GetArticleIndex();
            if (idx >= 0 && idx < static_cast<int>(articles.size())) {
                const std::string& link = articles[static_cast<size_t>(idx)].link;
                if (m_recorder)
                    m_recorder->RecordToggleSelection(link);
                spdlog::debug("toggle_selection link={}", link);
                core.ToggleSelection(link);
            }
        }
        return true;
    }

    // Export selected articles as markdown digest + PDF bundle
    // Exports that download PDFs run in the background; the status badge
    // shows their progress and outcome.
    if (key_bindings.matches(event, KeyBindings::Action::ExportSelectedDigest)) {
        if (!StartTaskOrExplain(core.StartExportSelectedDigest(), true))
            return true;
        if (m_recorder)
            m_recorder->RecordExportSelectedDigest("");
        spdlog::info("export_selected_digest started");
        return true;
    }

    // Export selected articles as a .tar.gz archive (digest + PDFs)
    if (key_bindings.matches(event, KeyBindings::Action::ExportDigestArchive)) {
        if (StartTaskOrExplain(core.StartExportSelectedDigestArchive(), true))
            spdlog::info("export_digest_archive started");
        return true;
    }

    // Export selected articles into the configured Obsidian vault
    if (key_bindings.matches(event, KeyBindings::Action::ExportToObsidian)) {
        if (m_config.get_obsidian_vault().empty() && core.GetSelectionCount() > 0 &&
            !core.IsBackgroundTaskRunning()) {
            err_msg = "No Obsidian vault set — add one in Settings (S), Export section";
            dialog_depth = Dialog::Error;
            return true;
        }
        if (!StartTaskOrExplain(core.StartExportSelectedToObsidian(), true))
            return true;
        if (m_recorder)
            m_recorder->RecordExportToObsidian("");
        spdlog::info("export_to_obsidian started");
        return true;
    }

    // Export daily digest to digest-YYYY-MM-DD.md
    if (key_bindings.matches(event, KeyBindings::Action::ExportDigest)) {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_val{};
        localtime_r(&t, &tm_val);
        char buf[32];
        std::strftime(buf, sizeof(buf), "digest-%Y-%m-%d.md", &tm_val);
        const std::string path = core.ExportPath(buf);
        if (core.ExportDailyDigest(path)) {
            if (m_recorder)
                m_recorder->RecordExportDailyDigest(path);
            spdlog::info("export_daily_digest path={}", path);
            success_msg = "Digest exported to " + path;
            dialog_depth = Dialog::Success;
        } else {
            spdlog::warn("export_daily_digest failed path={}", path);
            err_msg = "Digest export failed: " + path;
            dialog_depth = Dialog::Error;
        }
        return true;
    }

    // Open export dialog (only when viewing a project)
    if (key_bindings.matches(event, KeyBindings::Action::ExportProject)) {
        if (core.GetFilterView() == AppCore::FilterView::Project) {
            export_project_name = core.CurrentProject();
            export_format_index = 0;
            dialog_depth = Dialog::Export;
        }
        return true;
    }

    // Open import dialog
    if (key_bindings.matches(event, KeyBindings::Action::ImportProject)) {
        import_path.clear();
        dialog_depth = Dialog::Import;
        return true;
    }

    // Force full retrain
    if (key_bindings.matches(event, KeyBindings::Action::ForceRetrain)) {
        spdlog::info("force_retrain requested");
        core.ForceRetrain();
        if (m_recorder)
            m_recorder->RecordForceRetrain();
        return true;
    }

    // Open rating dialog for the focused article (ignores any active selection)
    if (key_bindings.matches(event, KeyBindings::Action::RateArticle)) {
        const auto& articles = core.GetCurrentArticles();
        if (!articles.empty()) {
            m_bulk_rating = false;
            int existing =
                core.GetArticleRating(articles[static_cast<size_t>(core.GetArticleIndex())].link);
            pending_rating = existing > 0 ? existing : 3;
            dialog_depth = Dialog::Rating;
        }
        return true;
    }

    // Open rating dialog for the entire selection
    if (key_bindings.matches(event, KeyBindings::Action::RateSelection)) {
        if (core.GetSelectionCount() > 0) {
            m_bulk_rating = true;
            pending_rating = 3;
            dialog_depth = Dialog::Rating;
        } else {
            err_msg = "No articles selected (Space to select)";
            dialog_depth = Dialog::Error;
        }
        return true;
    }

    // Open selected (or focused) article(s) in the default browser
    if (key_bindings.matches(event, KeyBindings::Action::OpenInBrowser)) {
        auto links = core.GetLinksToOpen();
        for (const auto& url : links) {
            // No shell: the link is passed to xdg-open as a single argument.
            Process::SpawnDetached({"xdg-open", url});
            spdlog::info("open_in_browser url={}", url);
        }
        if (!links.empty()) {
            success_msg = links.size() == 1
                              ? "Opening in browser: " + links[0]
                              : "Opening " + std::to_string(links.size()) + " articles in browser";
            dialog_depth = Dialog::Success;
        }
        return true;
    }

    // Pane navigation
    if (key_bindings.matches(event, KeyBindings::Action::MoveLeft)) {
        focused_pane = 0;
        title_start_position = 0;
        return true;
    }
    if (key_bindings.matches(event, KeyBindings::Action::MoveRight)) {
        focused_pane = 1;
        return true;
    }

    // Show detail toggle — mark focused article as read when detail opens
    if (key_bindings.matches(event, KeyBindings::Action::ShowDetail)) {
        show_detail = !show_detail;
        if (show_detail) {
            const auto& articles = core.GetCurrentArticles();
            if (!articles.empty()) {
                const auto& link = articles[static_cast<size_t>(core.GetArticleIndex())].link;
                core.MarkArticleRead(link);
            }
        }
        return true;
    }

    // Quit / dismiss any open dialog
    if (key_bindings.matches(event, KeyBindings::Action::Quit) || event == Event::Escape) {
        if (dialog_depth != Dialog::None) {
            dialog_depth = Dialog::None;
            err_msg = "";
            return true;
        }
        screen.Exit();
        return true;
    }

    // NewProject dialog events (handled here so quit above can dismiss first)
    if (dialog_depth == Dialog::NewProject) {
        return HandleNewProjectEvent(event);
    }

    // AssignProject dialog events
    if (dialog_depth == Dialog::AssignProject) {
        return HandleAssignProjectEvent(event);
    }

    return false;
}

bool ArxivApp::StartTaskOrExplain(bool started, bool needs_selection) {
    if (started)
        return true;
    if (core.IsBackgroundTaskRunning())
        err_msg = "Still working on: " + core.GetBackgroundTaskStatus().label;
    else if (needs_selection && core.GetSelectionCount() == 0)
        err_msg = "No articles selected (Space to select)";
    else
        err_msg = "Could not start the export";
    dialog_depth = Dialog::Error;
    return false;
}
