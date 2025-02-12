#include "Arxiv/App.hh"

#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

ArxivApp::ArxivApp(const std::vector<std::string> &topics) 
        : m_topics(topics), fetcher(topics), screen(ScreenInteractive::Fullscreen()) {

    spdlog::info("ArXiv Application Initialized");
    current_articles = fetcher.Fetch();

    db = std::make_unique<DatabaseManager>("articles.db");
    for(const auto &article : current_articles) {
        db->AddArticle(article);
    }

    projects = db->GetProjects();
    RefreshFilterOptions();
    FetchArticles();
    SetupUI();
}

void ArxivApp::SetupUI() {
    filter_menu = Menu(&filter_options, &filter_index)
        | CatchEvent([&](Event event) {
            if(event == Event::Character('j')) {
                filter_index = std::min(filter_index + 1, static_cast<int>(filter_options.size()) - 1);
                needs_refresh = true;
                return true;
            }
            if(event == Event::Character('k')) {
                filter_index = std::max(filter_index - 1, 0);
                needs_refresh = true;
                return true;
            }
            if(event == Event::Character('l')) {
                focused_pane = 1;
                return true;
            }
            if(event == Event::Character('p')) {
                AddProject();
                return true;
            }
            return false;
        });
    
    article_list = Container::Vertical({Menu(&current_titles, &article_index)})
        | CatchEvent([&](Event event) {
            if(event == Event::Character('j')) {
                article_index = std::min(article_index + 1, static_cast<int>(current_articles.size()) - 1);
                return true;
            }
            if(event == Event::Character('k')) {
                article_index = std::max(article_index - 1, 0);
                return true;
            }
            if(event == Event::Character('h')) {
                focused_pane = 0;
                return true;
            }
            if(event == Event::Character('b')) {
                ToggleBookmark(current_articles[static_cast<size_t>(article_index)]);
                return true;
            }
            if(event == Event::Character('p')) {
                AddArticleToProjects();
                return true;
            }
            return false;
        });

    article_pane = Renderer([&] {
        if(current_articles.empty()) {
            return vbox({
                text("Articles") | bold,
                separator(),
                text("No articles available") | dim | center,
                separator(),
                text("Try changing filters.") | dim | center,
            }) | border;
        }

        return vbox({
            text("Articles") | bold,
            separator(),
            article_list->Render() | frame | vscroll_indicator
        }) | border;
    });
    

    spdlog::info("[App]: Setting up detailed_view");
    detail_view = Renderer([&] {
        if(current_articles.empty() || article_index >= static_cast<int>(current_articles.size()))
            return window(text("Detail View"), text("No details available.") | center);
        Article article = current_articles[static_cast<size_t>(article_index)];
        spdlog::info("[App]: Title = {}", article.title);
        spdlog::info("[App]: Authors = {}", article.authors);
        spdlog::info("[App]: Link = {}", article.link);
        spdlog::info("[App]: Abstract = {}", article.abstract);
        return vbox({
            text("Title: " + article.title) | bold,
            text("Authors: " + article.authors),
            text("Link: " + article.link),
            separator(),
            paragraph("Abstract: \n" + article.abstract),
        }) | border;
    });

    main_container = Container::Tab({
        filter_menu,
        article_list,
        detail_view
    }, &focused_pane);

    spdlog::info("[App]: Setting up main renderer");
    main_renderer = Renderer(main_container, [&] {
        if(needs_refresh) {
            FetchArticles();
            needs_refresh = false;
        }

        int filter_width = FilterPaneWidth();
        int remaining_width = Terminal::Size().dimx - filter_width - border_size; 

        int articles_width = show_detail ? remaining_width / 3 : remaining_width;
        int detail_width = show_detail ? (remaining_width * 2) / 3 : 0;

        // auto title_style = [](const std::string &title, bool active) {
        //     return active ? text(title) | inverted | bold : text(title) | dim;
        // };

        std::vector<Element> panes = {
            filter_menu->Render() | border | size(WIDTH, EQUAL, filter_width),
            article_list->Render() | border | size(WIDTH, EQUAL, articles_width),
        };
        if(show_detail) {
            spdlog::info("[App]: Rendering detail");
            panes.push_back(detail_view->Render() | border | size(WIDTH, EQUAL, detail_width));
        }
        return hbox(std::move(panes));
    });

    spdlog::info("[App]: Setting up event handler");
    event_handler = CatchEvent(main_renderer, [&](Event event) {
        if(event == Event::Character('d')) {
            show_detail = !show_detail;
            return true;
        }
        if(event == Event::Character('q') || event == Event::Escape) {
            screen.Exit();
            return true;
        }
        return false;
    });

    spdlog::info("[App]: Successfully initialized UI");
}

void ArxivApp::AddProject() {
    std::string new_project;
    auto input = Input(&new_project, "New Project");
    auto button = Button("Add", [&] {
        if(!new_project.empty()) {
            db->AddProject(new_project);
            projects = db->GetProjects();
            RefreshFilterOptions();
        }
        screen.ExitLoopClosure()();
    });
    auto project_dialog = Renderer(Container::Vertical({
        input,
        button
    }), [&] {
        return vbox({
            text("Add New Project"),
            separator(),
            input->Render(),
            separator(),
            button->Render()
            }) | border;
    });
    auto project_event_handler = CatchEvent(project_dialog, [&](Event event) {
        if(event == Event::Return) {
            button->TakeFocus();
            button->OnEvent(Event::Return);
            return true;
        }
        if(event == Event::Escape) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });
    screen.Loop(project_event_handler);
}

void ArxivApp::AddArticleToProjects() {
    spdlog::info("[App]: Adding article to projects {}", article_index);
    if(current_articles.empty() || article_index >= static_cast<int>(current_articles.size())) {
        return;
    }

    Article &article = current_articles[static_cast<size_t>(article_index)];
    std::vector<bool> project_statuses(projects.size(), false);

    // Fetch the projects this article is already linked to
    std::vector<Article> linked_articles;
    for(size_t i = 0; i < projects.size(); ++i) {
        linked_articles = db->GetArticlesForProject(projects[i]);
        for(const auto &linked_article : linked_articles) {
            if(linked_article.link == article.link) {
                project_statuses[i] = true;
                break;
            }
        }
    }

    std::vector<Component> checkboxes;
    for(size_t i = 0; i < projects.size(); ++i) {
        auto status = std::make_shared<bool>(project_statuses[i]);
        checkboxes.push_back(Checkbox(&projects[i], status.get()));
    }
    auto checkbox_container = Container::Vertical({checkboxes});
    auto button = Button("Save", [&] {
        for(size_t i = 0; i < projects.size(); ++i) {
            if(project_statuses[i]) {
                db->LinkArticleToProject(article.link, projects[i]);
            } else {
                db->UnlinkArticleFromProject(article.link, projects[i]);
            }
        }
        screen.ExitLoopClosure()();
    });

    auto project_dialog = Renderer(Container::Vertical({
        checkbox_container,
        button
    }), [&] {
        return vbox({
            text("Add Article to Projects"),
            separator(),
            checkbox_container->Render(),
            separator(),
            button->Render()
        }) | border;
    });

    auto project_event_handler = CatchEvent(project_dialog, [&](Event event) {
        if(event == Event::Return) {
            button->OnEvent(Event::Return);
            return true;
        }
        if(event == Event::Escape) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });
    screen.Loop(project_event_handler);
}

void ArxivApp::FetchArticles() {
    spdlog::debug("[App]: Fetching articles for filter_index {}", filter_index);
    article_index = 0;
    std::vector<Article> new_articles;
    if(filter_index == 1) {
        new_articles = db->ListBookmarked();
    } else if(filter_index == 0) {
        new_articles = db->GetRecent(-1);
    } else if(filter_index == 2) {
        new_articles = db->GetRecent(1);
    } else if(filter_index >= 3) {
        new_articles = db->GetArticlesForProject(filter_options[static_cast<size_t>(filter_index)]);
    }

    spdlog::debug("[App]: Found {} articles", new_articles.size());
    current_articles = std::move(new_articles);
    RefreshTitles();
}

void ArxivApp::RefreshTitles() {
    current_titles.clear();
    for(const auto &art : current_articles) {
        std::string display_title = art.title;
        if(art.bookmarked) {
            display_title = "⭐ " + display_title;
        }
        current_titles.push_back(display_title);
    }
}

void ArxivApp::ToggleBookmark(Article &article) {
    article.bookmarked = !article.bookmarked;
    db->ToggleBookmark(article.link, article.bookmarked);
    RefreshTitles();
}

int ArxivApp::FilterPaneWidth() {
    int max_length = 0;
    for(const auto &option : filter_options) {
        max_length = std::max(max_length, static_cast<int>(option.size()));
    }

    return max_length + padding + arrow_size;
}

void ArxivApp::RefreshFilterOptions() {
    filter_options = {"All Articles", "Bookmarks", "Today"};
    for(const auto &project : projects) {
        filter_options.push_back(project);
    }
}
