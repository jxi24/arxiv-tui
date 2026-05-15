#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

#include <chrono>
#include <ctime>

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupMainRenderer() {
    main_renderer = Renderer(main_container, [&] {
        int filter_width = FilterPaneWidth();
        int remaining_width = Terminal::Size().dimx - filter_width - border_size;

        int articles_width = show_detail ? remaining_width / 2 : remaining_width;
        int detail_width   = show_detail ? remaining_width / 2 : 0;

        std::vector<Element> panes = {
            filter_pane->Render()  | size(WIDTH, EQUAL, filter_width),
            article_pane->Render() | size(WIDTH, EQUAL, articles_width),
        };
        if(show_detail) {
            panes.push_back(detail_view->Render() | size(WIDTH, EQUAL, detail_width));
        }
        Element body = hbox(std::move(panes)) | bgcolor(TextColors::base());

        auto fmt_key = [](std::string k) { return k == " " ? std::string("<space>") : k; };
        std::string quit_key = fmt_key(key_bindings.get_key(KeyBindings::Action::Quit));
        std::string help_key = fmt_key(key_bindings.get_key(KeyBindings::Action::ShowHelp));
        auto footer = hbox({
            text(" "),
            text(quit_key) | bold | color(TextColors::primary()),
            text(" quit  ") | color(TextColors::subtext()),
            text(help_key) | bold | color(TextColors::primary()),
            text(" keybindings ") | color(TextColors::subtext()),
            filler(),
        }) | bgcolor(TextColors::surface()) | size(HEIGHT, EQUAL, 1);

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
            }) | borderStyled(ROUNDED, TextColors::error()) | bgcolor(TextColors::surface()) | clear_under | center;
            document = dbox({document, error_dialog});
        } else if (dialog_depth == Dialog::Success) {
            auto success_dialog = vbox({
                text("OK") | bold | center | color(TextColors::primary()),
                separator() | color(TextColors::primary()),
                text(success_msg) | color(TextColors::text()),
                text("Press Enter or Esc to dismiss") | color(TextColors::subtext()),
            }) | borderStyled(ROUNDED, TextColors::primary()) | bgcolor(TextColors::surface()) | clear_under | center;
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
        } else if (dialog_depth == Dialog::CategoryFilter) {
            document = dbox({document, category_dialog->Render()});
        } else if (dialog_depth == Dialog::Settings) {
            document = dbox({document, settings_dialog->Render()});
        }

        if (show_help) {
            document = dbox({document, help_dialog->Render()});
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
            }) | bgcolor(TextColors::surface());

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
        if (show_help) return HandleHelpEvent(event);

        // Help toggle (open help from any state)
        if(key_bindings.matches(event, KeyBindings::Action::ShowHelp)) {
            ToggleHelp();
            return true;
        }

        // DateRange dialog: handle events, or open via key binding
        if (dialog_depth == Dialog::DateRange) return HandleDateRangeEvent(event);
        if (key_bindings.matches(event, KeyBindings::Action::SetDateRange) &&
            core.GetFilterView() == AppCore::FilterView::Range) {
            dialog_depth = Dialog::DateRange;
            date_input_mode = DateInputMode::Start;
            start_date.clear();
            end_date.clear();
            return true;
        }

        // Search dialog: handle events, or open via key binding
        if (dialog_depth == Dialog::Search) return HandleSearchEvent(event);
        if (key_bindings.matches(event, KeyBindings::Action::Search)) {
            dialog_depth = Dialog::Search;
            search_query.clear();
            selected_search_option = 0;
            return true;
        }

        if (dialog_depth == Dialog::Rating)         return HandleRatingEvent(event);
        if (dialog_depth == Dialog::Notes)          return HandleNotesEvent(event);
        if (dialog_depth == Dialog::Export)         return HandleExportEvent(event);

        // Success dialog dismiss
        if (dialog_depth == Dialog::Success) {
            if (event == Event::Return || event == Event::Escape) {
                dialog_depth = Dialog::None;
                success_msg = "";
            }
            return true;
        }

        if (dialog_depth == Dialog::Import)         return HandleImportEvent(event);
        if (dialog_depth == Dialog::CategoryFilter) return HandleCategoryFilterEvent(event);
        if (dialog_depth == Dialog::KeywordEditor)  return HandleKeywordEditorEvent(event);
        if (dialog_depth == Dialog::Settings)       return HandleSettingsEvent(event);

        return HandleGlobalEvent(event);
    });
}

bool ArxivApp::HandleGlobalEvent(ftxui::Event event) {
    // Open notes editor (only when viewing a project with an article selected)
    if (key_bindings.matches(event, KeyBindings::Action::EditNote)) {
        if (core.GetFilterView() == AppCore::FilterView::Project) {
            auto articles = core.GetCurrentArticles();
            if (!articles.empty()) {
                note_project_name = core.GetProjectNameForFilter(core.GetFilterIndex());
                note_article_link = articles[static_cast<size_t>(core.GetArticleIndex())].link;
                note_edit_text = core.GetProjectNote(note_project_name, note_article_link);
                dialog_depth = Dialog::Notes;
            }
        }
        return true;
    }

    // Export BibTeX for current article
    if (key_bindings.matches(event, KeyBindings::Action::ExportBibTeX)) {
        auto articles = core.GetCurrentArticles();
        if (!articles.empty()) {
            const auto& art = articles[static_cast<size_t>(core.GetArticleIndex())];
            std::string path = art.id() + ".bib";
            bool ok = core.ExportArticleBibTeX(art, path);
            if (m_recorder) m_recorder->RecordExportArticleBibTeX(art.link, path);
            if (ok) {
                success_msg = "Exported to " + path;
                dialog_depth = Dialog::Success;
            } else {
                err_msg = "BibTeX export failed: " + path;
                dialog_depth = Dialog::Error;
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

    // Open arXiv category filter
    if (key_bindings.matches(event, KeyBindings::Action::FilterCategories)) {
        category_selected_index = 0;
        dialog_depth = Dialog::CategoryFilter;
        return true;
    }

    // Open settings dialog
    if (key_bindings.matches(event, KeyBindings::Action::Settings)) {
        settings_section     = 0;
        settings_field_index = 0;
        settings_editing     = false;
        settings_edit_buffer.clear();
        m_config = core.GetConfig();
        dialog_depth = Dialog::Settings;
        return true;
    }

    // Toggle selection of focused article (for Export Selected Digest)
    if (key_bindings.matches(event, KeyBindings::Action::ToggleSelection)) {
        auto articles = core.GetCurrentArticles();
        if (!articles.empty()) {
            int idx = core.GetArticleIndex();
            if (idx >= 0 && idx < static_cast<int>(articles.size())) {
                if (m_recorder) m_recorder->RecordEvent(
                    "app/toggle_selection",
                    "link=" + articles[static_cast<size_t>(idx)].link);
                core.ToggleSelection(articles[static_cast<size_t>(idx)].link);
            }
        }
        return true;
    }

    // Export selected articles as markdown digest + PDF bundle
    if (key_bindings.matches(event, KeyBindings::Action::ExportSelectedDigest)) {
        std::string dir = core.ExportSelectedDigest();
        if (dir.empty()) {
            err_msg = core.GetSelectionCount() == 0
                          ? "No articles selected (Space to select)"
                          : "Failed to export digest";
            dialog_depth = Dialog::Error;
        } else {
            success_msg = "Digest exported to " + dir;
            dialog_depth = Dialog::Success;
        }
        return true;
    }

    // Export selected articles into the configured Obsidian vault
    if (key_bindings.matches(event, KeyBindings::Action::ExportToObsidian)) {
        std::string path = core.ExportSelectedToObsidian();
        if (path.empty()) {
            if (core.GetSelectionCount() == 0)
                err_msg = "No articles selected (Space to select)";
            else
                err_msg = "Obsidian export failed — set 'obsidian_vault' in .arxiv-tui.yml";
            dialog_depth = Dialog::Error;
        } else {
            success_msg = "Exported to Obsidian vault: " + path;
            dialog_depth = Dialog::Success;
        }
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
        if (core.ExportDailyDigest(buf)) {
            success_msg = std::string("Digest exported to ") + buf;
            dialog_depth = Dialog::Success;
        } else {
            err_msg = std::string("Digest export failed: ") + buf;
            dialog_depth = Dialog::Error;
        }
        return true;
    }

    // Open export dialog (only when viewing a project)
    if (key_bindings.matches(event, KeyBindings::Action::ExportProject)) {
        if (core.GetFilterView() == AppCore::FilterView::Project) {
            export_project_name = core.GetProjectNameForFilter(core.GetFilterIndex());
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
        core.ForceRetrain();
        if (m_recorder) m_recorder->RecordForceRetrain();
        return true;
    }

    // Open rating dialog
    if (key_bindings.matches(event, KeyBindings::Action::RateArticle)) {
        auto articles = core.GetCurrentArticles();
        if (!articles.empty()) {
            dialog_depth = Dialog::Rating;
            int existing = core.GetArticleRating(
                articles[static_cast<size_t>(core.GetArticleIndex())].link);
            pending_rating = existing > 0 ? existing : 3;
        }
        return true;
    }

    // Pane navigation
    if(key_bindings.matches(event, KeyBindings::Action::MoveLeft)) {
        focused_pane = 0;
        title_start_position = 0;
        return true;
    }
    if(key_bindings.matches(event, KeyBindings::Action::MoveRight)) {
        focused_pane = 1;
        return true;
    }

    // Show detail toggle
    if(key_bindings.matches(event, KeyBindings::Action::ShowDetail)) {
        show_detail = !show_detail;
        return true;
    }

    // Quit / dismiss any open dialog
    if(key_bindings.matches(event, KeyBindings::Action::Quit) || event == Event::Escape) {
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
