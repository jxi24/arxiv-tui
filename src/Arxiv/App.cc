#include "Arxiv/App.hh"
#include "spdlog/spdlog.h"
#include "ftxui/component/screen_interactive.hpp"

using namespace ftxui;

Arxiv::ArxivApp::ArxivApp(const std::vector<std::string> &topics)
    : m_topics(topics), db("articles.db"), fetcher(topics) {

    spdlog::info("ArXiv Application Initialized");
    // Fetch articles
    auto articles = fetcher.Fetch();

    // Store articles in database
    for(const auto &article : articles) {
        db.AddArticle(article);
    }

    InitializeUI();
}

void Arxiv::ArxivApp::InitializeUI() {
    // Initialize tab menu
    std::vector<std::string> tab_entries = {"All Articles", "Bookmarked"};
    tab_menu = Menu(&tab_entries, &current_tab);

    // Initialize article list with callbacks
    RefreshArticles();

    spdlog::debug("[App]: Initializing article list");
    article_list = std::make_shared<ArticleListComponent>(
        current_articles,
        [this](const Article &article) { ShowDetailView(article); },
        [this](Article &article) { ToggleBookmark(article); }
    );
    spdlog::debug("[App]: Article list initialized");

    // Create detail view (initially hidden)
    detail_view = Container::Vertical({});

    // Create main layout with tab change detection
    auto main_layout = Container::Horizontal({
        tab_menu,
        article_list,
    });

    // Add tab change detection
    main_layout |= CatchEvent([this](Event event) {
        spdlog::trace("[App]: Tab is {} and view is {}", current_tab, current_view);
        // spdlog::debug("[App]: Event received: {}", event.DebugString());
        // If tab chaged, refresh articles
        if(event.is_mouse() || event.is_character()) {
            // int previous_tab = current_tab;
            // Let the event propagate to update current_tab
            bool handled = false;
            if(current_view == 0) {
                spdlog::info("here");
                handled = article_list->OnEvent(event);
            } else if(current_view == 1) {
                handled = detail_view->OnEvent(event);
            }
            // handled = tab_menu->OnEvent(event);

            // After the event, check if tab changed
            // if(current_tab != previous_tab) {
            //     spdlog::debug("[App]: Switching from tab {} to tab {}",
            //                   previous_tab, current_tab);
            //     RefreshArticles();
            //     handled = true;
            // }

            spdlog::trace("[App]: Tab is {} and view is {}", current_tab, current_view);
            return handled;
        }
        return false;
    });

    // Create component stack for main view and detail view
    main_container = CatchEvent(
        Container::Tab(
            {
                main_layout,
                detail_view
            },
            &current_view
        ),
        [&](Event event) {
            spdlog::info("[App]: Received event: {}", event.DebugString());

            if(event == Event::Character('q') || event == Event::Escape) {
                spdlog::info("[App]: Exiting application...");
                ScreenInteractive::Active()->ExitLoopClosure()();
                return true;
            }

            if(event == Event::Character('h')) {
                spdlog::info("[App]: Returning to main view...");
                current_view = 0;
                ScreenInteractive::Active()->Post(Event::Custom);
                return true;
            }
            return false;
        }
    );

    spdlog::debug("[App]: UI Initialized");
}

void Arxiv::ArxivApp::RefreshArticles() {
    spdlog::debug("[App]: Refreshing articles");
    spdlog::trace("[App]: Tab is {}", current_tab);
    // TODO: Figure out why the current_tab randomly switches
    // current_articles = (current_tab == 0)
    //     ? db.GetRecent(-1)
    //     : db.ListBookmarked();
    current_articles = db.GetRecent(-1);
    spdlog::debug("[App]: Found {} articles", current_articles.size());
}

void Arxiv::ArxivApp::ShowDetailView(const Article &article) {
    spdlog::debug("[App]: Creating detail view for {}", article.link);

    // Clear and update container
    detail_view->DetachAllChildren();
    detail_view->Add(Container::Vertical({
        Renderer([&article] {
            return window(
                text("Article Details") | hcenter,
                vbox({
                    text("Title: " + article.title),
                    separator(),
                    text("Authors: " + article.authors),
                    separator(),
                    text("Link: " + article.link),
                    separator(),
                    paragraph("Abstract: " + article.abstract),
                    text("Press 'h' to return") | dim
                })
            );
        }), 
        Renderer([] { return text(""); }) // Spacer
    }));

    detail_view |= CatchEvent([this](Event event) {
        if(event == Event::Character('h')) {
            current_view = 0; // Switch back to main view
            ScreenInteractive::Active()->Post(Event::Custom);
            return true;
        }
        return false;
    });

    current_view = 1; // Switch to detail view
    ScreenInteractive::Active()->Post(Event::Custom);
    spdlog::debug("[App]: Finished. Setting view to {}", current_view);
}

void Arxiv::ArxivApp::ToggleBookmark(Article &article) {
    article.bookmarked = !article.bookmarked;
    spdlog::trace("[App]: Tab is {}", current_tab);
    db.ToggleBookmark(article.link, article.bookmarked);
    spdlog::trace("[App]: Tab is {}", current_tab);
    RefreshArticles();
}

void Arxiv::ArxivApp::Run() {
    // try {
        spdlog::debug("[App]: Starting screen");
        auto screen = ScreenInteractive::Fullscreen();
        screen.Loop(main_container);
        spdlog::debug("[App]: Exiting");
    // } catch (const std::exception &e) {
    //     spdlog::error("[App]: Error in application {}", e.what());
    // }
}
