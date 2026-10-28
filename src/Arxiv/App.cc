// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"

#include "Arxiv/Article.hh"
#include "Arxiv/Views/Colors.hh"

#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

ArxivApp::ArxivApp(const Config& config, const std::string& config_path, ReplayRecorder* recorder)
    : core(config,
           std::make_unique<DatabaseManager>(config.get_db_file()),
           std::make_unique<Fetcher>(config.get_topics(), config.get_download_dir()),
           AppCore::FetchMode::Async,
           recorder)
    , key_bindings(config.get_key_mappings())
    , m_config_path(config_path)
    , m_config(config)
    , screen(ScreenInteractive::Fullscreen()) {
    m_recorder = recorder;

    spdlog::info("ArXiv Application Initialized");
    if (m_recorder)
        m_recorder->RecordEvent("app/ctor_after_appcore");
    core.ReloadKeywords();
    if (m_recorder)
        m_recorder->RecordEvent("app/setup_ui_begin");
    Arxiv::SetTheme(m_config.get_theme(), m_config.get_theme_colors());
    SetupUI();
    // Settings that were rejected while loading config.yml: say which, and
    // that their defaults are in use.
    if (!m_config.get_warnings().empty())
        dialog_depth = Dialog::ConfigWarning;
    if (m_recorder)
        m_recorder->RecordEvent("app/setup_ui_end");
    // auto_refresh_minutes was read but never acted on; 0 keeps it off.
    if (config.get_auto_refresh_minutes() > 0)
        core.StartAutoRefresh();
}

#ifdef TESTING
ArxivApp::ArxivApp(Config cfg,
                   std::unique_ptr<DatabaseManager> db,
                   std::unique_ptr<Fetcher> fetcher,
                   KeyBindings kb,
                   const std::string& config_path)
    : core(cfg, std::move(db), std::move(fetcher))
    , key_bindings(std::move(kb))
    , m_config_path(config_path)
    , m_config(cfg)
    , screen(ScreenInteractive::Fullscreen()) {
    Arxiv::SetTheme(m_config.get_theme(), m_config.get_theme_colors());
    SetupUI();
    // Settings that were rejected while loading config.yml: say which, and
    // that their defaults are in use.
    if (!m_config.get_warnings().empty())
        dialog_depth = Dialog::ConfigWarning;
    // No refresh thread: tests drive events directly via GetEventHandler().
}
#endif

void ArxivApp::UpdateTitleScrollPositions() {
    if (focused_pane != 1 || !show_detail)
        return;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<float>(now - last_update).count();
    spdlog::trace("[App]: time elapsed = {}", elapsed);

    title_start_position += scroll_speed * elapsed;
    last_update = now;
}

void ArxivApp::UpdateVisibleRange() {
    const auto& articles = core.GetCurrentArticles();
    if (articles.empty())
        return;

    int current_index = core.GetArticleIndex();
    int margin = std::min(m_config.get_scroll_margin(), visible_rows / 2);

    if (current_index < top_article_index + margin) {
        top_article_index = std::max(0, current_index - margin);
    } else if (current_index >= top_article_index + visible_rows - margin) {
        int max_top = static_cast<int>(articles.size()) - visible_rows;
        top_article_index =
            std::min(current_index - visible_rows + margin + 1, std::max(0, max_top));
    }
}

void ArxivApp::ToggleHelp() {
    show_help = !show_help;
    if (show_help)
        help_search_query.clear();
}

void ArxivApp::SetupUI() {
    SetupFilterPane();
    SetupArticlePane();
    SetupDetailView();

    main_container = Container::Tab({filter_pane, article_pane, detail_view}, &focused_pane);

    SetupAssignProjectDialog();
    SetupHelpDialog();
    SetupDateRangeDialog();
    SetupSearchDialog();
    SetupRatingDialog();
    SetupNotesDialog();
    SetupExportDialog();
    SetupImportDialog();
    SetupKeywordEditorDialog();
    SetupFollowAuthorsDialog();
    SetupTagsDialog();
    SetupCategoryFilterDialog();
    SetupSettingsDialog();
    SetupMainRenderer();
    SetupEventHandler();

    // Article update callback triggers a UI refresh
    core.SetArticleUpdateCallback([&]() {
        if (m_recorder)
            m_recorder->RecordEvent("app/article_update_callback");
        RefreshUI();
    });

    // Refresh / animation thread
    refresh_ui = std::thread([&] {
        if (m_recorder)
            m_recorder->RecordEvent("app/refresh_thread_started");
        long long tick = 0;
        while (refresh_ui_continue) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(0.05s);
            // Redraw only when something changed or is animating; an idle
            // TUI used to repaint 20 times a second.
            screen.Post([&] {
                bool dirty = core.TryRefetchIfNeeded();
                if (focused_pane == 1 && show_detail) { // scrolling title
                    UpdateTitleScrollPositions();
                    dirty = true;
                }
                const auto task = core.GetBackgroundTaskStatus();
                const bool badge_visible =
                    task.running ||
                    (!task.label.empty() &&
                     std::chrono::steady_clock::now() - task.finished_at < std::chrono::seconds(9));
                // Keep repainting a moment past a toast's end so it is cleared.
                const bool toast_visible =
                    !m_toast.empty() &&
                    std::chrono::steady_clock::now() < m_toast_until + std::chrono::seconds(1);
                if (dirty || badge_visible || toast_visible || core.IsFetching())
                    screen.PostEvent(Event::Custom);
            });
            if (m_recorder && (tick++ % 20) == 0) {
                m_recorder->RecordEvent("app/refresh_tick",
                                        std::string("fetching=") + (core.IsFetching() ? "1" : "0"));
            }
        }
    });
}

void ArxivApp::RefreshUI() { screen.PostEvent(Event::Custom); }

std::vector<std::pair<std::string, std::string>> ArxivApp::FooterHints() const {
    using A = KeyBindings::Action;
    std::vector<std::pair<A, const char*>> hints;
    if (focused_pane == 0) {
        hints = {{A::MoveRight, "articles"}, {A::CreateProject, "new project"}};
        if (!core.CurrentProject().empty())
            hints.push_back({A::DeleteProject, "delete project"});
    } else if (core.GetSelectionCount() > 0) {
        hints = {{A::ToggleSelection, "select"},
                 {A::ClearSelection, "clear"},
                 {A::RateSelection, "rate selection"},
                 {A::TagArticle, "tags"},
                 {A::ExportSelectedDigest, "digest"},
                 {A::ExportToObsidian, "obsidian"}};
    } else {
        hints = {{A::ShowDetail, "detail"},
                 {A::Bookmark, "bookmark"},
                 {A::RateArticle, "rate"},
                 {A::TagArticle, "tags"},
                 {A::ToggleSelection, "select"}};
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [action, label] : hints)
        out.emplace_back(KeyBindings::DisplayKey(key_bindings.get_key(action)), label);
    return out;
}

void ArxivApp::ShowToast(std::string message) {
    m_toast = std::move(message);
    m_toast_until = std::chrono::steady_clock::now() + kToastDuration;
}

bool ArxivApp::ToastVisible() const {
    return !m_toast.empty() && std::chrono::steady_clock::now() < m_toast_until;
}

int ArxivApp::ScreenWidth() const {
    return m_screen_box.x_max > m_screen_box.x_min ? m_screen_box.x_max - m_screen_box.x_min + 1
                                                   : Terminal::Size().dimx;
}

int ArxivApp::ScreenHeight() const {
    return m_screen_box.y_max > m_screen_box.y_min ? m_screen_box.y_max - m_screen_box.y_min + 1
                                                   : Terminal::Size().dimy;
}

int ArxivApp::FilterPaneWidth() {
    int max_length = 0;
    const auto& options = core.GetFilterOptions();
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        int length = static_cast<int>(options[static_cast<std::size_t>(i)].size());
        if (auto count = core.GetFilterCount(i))
            length += static_cast<int>(std::to_string(*count).size()) + 2;
        max_length = std::max(max_length, length);
    }
    return max_length + padding + arrow_size;
}
