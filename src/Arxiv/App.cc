// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Article.hh"

#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

ArxivApp::ArxivApp(const Config& config, const std::string& config_path, ReplayRecorder* recorder)
    : core(config,
           std::make_unique<DatabaseManager>("articles.db"),
           std::make_unique<Fetcher>(config.get_topics(), config.get_download_dir()),
           AppCore::FetchMode::Async,
           recorder)
    , key_bindings(config.get_key_mappings())
    , m_config_path(config_path)
    , m_config(config)
    , screen(ScreenInteractive::Fullscreen()) {
    m_recorder = recorder;

    spdlog::info("ArXiv Application Initialized");
    if (m_recorder) m_recorder->RecordEvent("app/ctor_after_appcore");
    core.ReloadKeywords();
    if (m_recorder) m_recorder->RecordEvent("app/setup_ui_begin");
    SetupUI();
    if (m_recorder) m_recorder->RecordEvent("app/setup_ui_end");
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
    SetupUI();
    // No refresh thread: tests drive events directly via GetEventHandler().
}
#endif

void ArxivApp::UpdateTitleScrollPositions() {
    if(focused_pane != 1 || !show_detail) return;
    auto now = std::chrono::steady_clock::now();
    auto article = core.GetCurrentArticles()[static_cast<size_t>(core.GetArticleIndex())];

    auto elapsed = std::chrono::duration<float>(now - last_update).count();
    spdlog::trace("[App]: time elapsed = {}", elapsed);

    title_start_position += scroll_speed * elapsed;
    last_update = now;
}

void ArxivApp::UpdateVisibleRange() {
    auto articles = core.GetCurrentArticles();
    if(articles.empty()) return;

    int current_index = core.GetArticleIndex();
    int margin = std::min(m_config.get_scroll_margin(), visible_rows / 2);

    if(current_index < top_article_index + margin) {
        top_article_index = std::max(0, current_index - margin);
    } else if(current_index >= top_article_index + visible_rows - margin) {
        int max_top = static_cast<int>(articles.size()) - visible_rows;
        top_article_index = std::min(current_index - visible_rows + margin + 1,
                                     std::max(0, max_top));
    }
}

void ArxivApp::ToggleHelp() {
    show_help = !show_help;
}

void ArxivApp::SetupUI() {
    SetupFilterPane();
    SetupArticlePane();
    SetupDetailView();

    main_container = Container::Tab({
        filter_pane,
        article_pane,
        detail_view
    }, &focused_pane);

    SetupAssignProjectDialog();
    SetupHelpDialog();
    SetupDateRangeDialog();
    SetupSearchDialog();
    SetupRatingDialog();
    SetupNotesDialog();
    SetupExportDialog();
    SetupImportDialog();
    SetupKeywordEditorDialog();
    SetupCategoryFilterDialog();
    SetupSettingsDialog();
    SetupMainRenderer();
    SetupEventHandler();

    // Article update callback triggers a UI refresh
    core.SetArticleUpdateCallback([&]() {
        if (m_recorder) m_recorder->RecordEvent("app/article_update_callback");
        RefreshUI();
    });

    // Refresh / animation thread
    refresh_ui = std::thread([&] {
        if (m_recorder) m_recorder->RecordEvent("app/refresh_thread_started");
        long long tick = 0;
        while (refresh_ui_continue) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(0.05s);
            screen.Post([&] {
                UpdateTitleScrollPositions();
                core.TryRefetchIfNeeded();
            });
            screen.Post(Event::Custom);
            if (m_recorder && (tick++ % 20) == 0) {
                m_recorder->RecordEvent("app/refresh_tick",
                                        std::string("fetching=") + (core.IsFetching() ? "1" : "0"));
            }
        }
    });
}

void ArxivApp::RefreshUI() {
    screen.PostEvent(Event::Custom);
}

int ArxivApp::FilterPaneWidth() {
    int max_length = 0;
    for(const auto& option : core.GetFilterOptions()) {
        max_length = std::max(max_length, static_cast<int>(option.size()));
    }
    return max_length + padding + arrow_size;
}
