#include "Arxiv/App.hh"
#include "Arxiv/Article.hh"

#include "spdlog/spdlog.h"
#include <ftxui/dom/elements.hpp>

using namespace ftxui;
using namespace Arxiv;

ArxivApp::ArxivApp(const std::vector<std::string>& topics)
    : core(topics,
           std::make_unique<DatabaseManager>("articles.db"),
           std::make_unique<Fetcher>(topics))
    , screen(ScreenInteractive::Fullscreen()) {
    
    spdlog::info("ArXiv Application Initialized");
    SetupUI();
}

void ArxivApp::UpdateTitleScrollPositions() {
    if(focused_pane != 1 || !show_detail) return;
    auto now = std::chrono::steady_clock::now();
    auto article = core.GetCurrentArticles()[static_cast<size_t>(core.GetArticleIndex())];
    
    auto elapsed = std::chrono::duration<float>(now - last_update).count();
    spdlog::info("[App]: time elapsed = {}", elapsed);
        
    // Update scroll position based on elapsed time
    title_start_position += scroll_speed * elapsed;
    last_update = now;
}

void ArxivApp::SetupUI() {
    filter_menu = Menu(&core.GetFilterOptions(), &core.GetFilterIndex())
        | CatchEvent([&](Event event) {
            if(event == Event::Character('j')) {
                core.SetFilterIndex(std::min(core.GetFilterIndex() + 1, 
                    static_cast<int>(core.GetFilterOptions().size()) - 1));
                return true;
            }
            if(event == Event::Character('k')) {
                core.SetFilterIndex(std::max(core.GetFilterIndex() - 1, 0));
                return true;
            }
            if(event == Event::Character('l')) {
                focused_pane = 1;
                return true;
            }
            if(event == Event::Character('p')) {
                dialog_depth = 1;
                new_project_name.clear();
                return true;
            }
            if(event == Event::Character('x')) {
                if(core.GetFilterIndex() > 2) {
                    core.RemoveProject(core.GetFilterOptions()[static_cast<size_t>(core.GetFilterIndex())]);
                }
                return true;
            }
            return false;
        });
    
    article_list = Container::Vertical({Menu(&core.GetCurrentTitles(), &core.GetArticleIndex())})
        | CatchEvent([&](Event event) {
            if(event == Event::Character('j')) {
                core.SetArticleIndex(std::min(core.GetArticleIndex() + 1, 
                    static_cast<int>(core.GetCurrentArticles().size()) - 1));
                title_start_position = 0;
                return true;
            }
            if(event == Event::Character('k')) {
                core.SetArticleIndex(std::max(core.GetArticleIndex() - 1, 0));
                title_start_position = 0;
                return true;
            }
            if(event == Event::Character('h')) {
                focused_pane = 0;
                return true;
            }
            if(event == Event::Character('b')) {
                auto articles = core.GetCurrentArticles();
                if(!articles.empty()) {
                    core.ToggleBookmark(articles[static_cast<size_t>(core.GetArticleIndex())].link);
                }
                return true;
            }
            if(event == Event::Character('p')) {
                dialog_depth = 2;
                // Initialize project selections
                auto projects = core.GetProjects();
                auto articles = core.GetCurrentArticles();
                
                // Get current article's projects
                selected_projects.clear();
                checkbox_states.clear();
                if (!articles.empty()) {
                    selected_projects = std::set<std::string>(
                        core.GetProjectsForArticle(articles[static_cast<size_t>(core.GetArticleIndex())].link).begin(),
                        core.GetProjectsForArticle(articles[static_cast<size_t>(core.GetArticleIndex())].link).end()
                    );
                }
                
                // Initialize checkbox states
                for(const auto& project : projects) {
                    checkbox_states[project] = selected_projects.count(project) > 0;
                }
                selected_project_index = 0;
                return true;
            }
            return false;
        });

    filter_pane = Renderer(filter_menu, [&] {
        return vbox({
            text("Filters") | bold,
            separator(),
            filter_menu->Render() | vscroll_indicator | frame
        }) | border;
    });

    article_pane = Renderer(article_list, [&] {
        auto articles = core.GetCurrentArticles();
        if(articles.empty()) {
            return vbox({
                text("Articles") | bold,
                separator(),
                text("No articles available") | dim | center,
                separator(),
                text("Try changing filters.") | dim | center,
            }) | border;
        }

        Elements menu_items;
        for(size_t i = 0; i < articles.size(); ++i) {
            const auto& article = articles[i];
            std::string title = article.title;
            
            // Create title element with proper focus handling
            Element title_element;
            if(i == static_cast<size_t>(core.GetArticleIndex())) {
                // Only scroll the focused title
                size_t start_pos = static_cast<size_t>(std::floor(title_start_position)) % article.title.length();
                int filter_width = FilterPaneWidth();
                int remaining_width = Terminal::Size().dimx - filter_width - border_size; 
                int articles_width = show_detail ? remaining_width / 3 : remaining_width;
                // Create a sliding window of the title
                std::string visible_title;
                if(title.length() > static_cast<size_t>(articles_width - 2)) {
                    title = title.substr(start_pos) + "    " + title.substr(0, start_pos);
                }
                title = "> " + title;
                title_element = text(title) | bold;
            } else {
                title = "  " + title;
                title_element = text(title);
            }
            
            menu_items.push_back(title_element);
        }

        return vbox({
            text("Articles") | bold,
            separator(),
            vbox(menu_items) | vscroll_indicator | frame
        }) | border;
    });

    detail_view = Renderer([&] {
        auto articles = core.GetCurrentArticles();
        if(articles.empty() || core.GetArticleIndex() >= static_cast<int>(articles.size())) {
            return window(text("Detail View"), text("No details available.") | center);
        }
        
        const auto& article = articles[static_cast<size_t>(core.GetArticleIndex())];
        return vbox({
            text("Title: " + article.title) | bold,
            text("Authors: " + article.authors),
            text("Link: " + article.link),
            separator(),
            paragraph("Abstract: \n" + article.abstract),
        }) | border;
    });

    main_container = Container::Tab({
        filter_pane,
        article_pane,
        detail_view
    }, &focused_pane);

    // Create project dialog components
    project_checkbox_container = Container::Vertical({});
    project_dialog = Renderer([&] {
        if (dialog_depth != 2) return emptyElement();
        
        auto projects = core.GetProjects();
        if (projects.empty()) {
            return vbox({
                text("Add to Projects") | bold,
                separator(),
                text("No projects available. Create a project first.") | center,
                separator(),
                hbox({
                    text("Press Esc to close"),
                }) | center,
            }) | border | clear_under | center;
        }

        std::vector<Element> menu_items;
        for(size_t i = 0; i < projects.size(); ++i) {
            const auto& project = projects[i];
            std::string prefix = checkbox_states[project] ? "[X] " : "[ ] ";
            auto item = text(prefix + project);
            if(i == static_cast<size_t>(selected_project_index)) {
                item |= inverted;
            }
            menu_items.push_back(item);
        }

        return vbox({
            text("Add to Projects") | bold,
            separator(),
            vbox(menu_items) | vscroll_indicator | frame,
            separator(),
            hbox({
                text("Use j/k to navigate, Space to toggle, Enter to save, Esc to cancel"),
            }) | center,
        }) | border | clear_under | center;
    });

    // Update project checkboxes when projects change
    core.SetProjectUpdateCallback([&]() {
        auto projects = core.GetProjects();
        project_checkbox_container->DetachAllChildren();
        
        for(const auto& project : projects) {
            if(checkbox_states.find(project) == checkbox_states.end()) {
                checkbox_states[project] = false;
            }
            project_checkbox_container->Add(Checkbox(project, &checkbox_states[project]));
        }
    });

    main_renderer = Renderer(main_container, [&] {
        int filter_width = FilterPaneWidth();
        int remaining_width = Terminal::Size().dimx - filter_width - border_size; 

        int articles_width = show_detail ? remaining_width / 3 : remaining_width;
        int detail_width = show_detail ? (remaining_width * 2) / 3 : 0;

        std::vector<Element> panes = {
            filter_pane->Render() | size(WIDTH, EQUAL, filter_width),
            article_pane->Render() | size(WIDTH, EQUAL, articles_width),
        };
        if(show_detail) {
            panes.push_back(detail_view->Render() | size(WIDTH, EQUAL, detail_width));
        }
        Element document = hbox(std::move(panes));

        if (dialog_depth == 1) {
            auto new_project_dialog = vbox({
                text("New Project") | bold,
                separator(),
                text("Enter project name: " + new_project_name),
                separator(),
                hbox({
                    text("Press Enter to create, Esc to cancel"),
                }) | center,
            }) | border | clear_under | center;

            document = dbox({
                document,
                new_project_dialog,
            });
        } else if (dialog_depth == 2) {
            document = dbox({
                document,
                project_dialog->Render(),
            });
        }

        return document;
    });

    event_handler = CatchEvent(main_renderer, [&](Event event) {
        if(event == Event::Character('d')) {
            show_detail = !show_detail;
            return true;
        }
        if(event == Event::Character('q') || event == Event::Escape) {
            if (dialog_depth > 0) {
                dialog_depth = 0;
                return true;
            }
            screen.Exit();
            return true;
        }

        if (dialog_depth == 1) {
            if (event == Event::Return) {
                if (!new_project_name.empty()) {
                    core.AddProject(new_project_name);
                }
                dialog_depth = 0;
                return true;
            }
            if (event.is_character()) {
                new_project_name += event.character();
                return true;
            }
            if (event == Event::Backspace) {
                if (!new_project_name.empty()) {
                    new_project_name.pop_back();
                }
                return true;
            }
        } else if (dialog_depth == 2) {
            if (event == Event::Return) {
                auto articles = core.GetCurrentArticles();
                if (!articles.empty()) {
                    auto article = articles[static_cast<size_t>(core.GetArticleIndex())];
                    for(const auto& [project, selected] : checkbox_states) {
                        if(selected) {
                            core.LinkArticleToProject(article.link, project);
                        } else {
                            core.UnlinkArticleFromProject(article.link, project);
                        }
                    }
                }
                dialog_depth = 0;
                return true;
            }
            if (event == Event::Character('j')) {
                auto projects = core.GetProjects();
                if(selected_project_index < static_cast<int>(projects.size()) - 1) {
                    selected_project_index++;
                }
                return true;
            }
            if (event == Event::Character('k')) {
                if(selected_project_index > 0) {
                    selected_project_index--;
                }
                return true;
            }
            if (event == Event::Character(' ')) {
                auto projects = core.GetProjects();
                if(selected_project_index < static_cast<int>(projects.size())) {
                    const auto& project = projects[static_cast<size_t>(selected_project_index)];
                    checkbox_states[project] = !checkbox_states[project];
                }
                return true;
            }
        }
        return false;
    });

    // Set up UI refresh callback
    core.SetArticleUpdateCallback([&]() { RefreshUI(); });

    // Setup animation
    refresh_ui = std::thread([&] {
        while (refresh_ui_continue) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(0.05s);
            screen.Post([&] { UpdateTitleScrollPositions(); });
            screen.Post(Event::Custom);
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
