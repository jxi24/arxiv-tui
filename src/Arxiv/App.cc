#include "Arxiv/App.hh"
#include "Arxiv/Article.hh"

#include "spdlog/spdlog.h"
#include <ftxui/dom/elements.hpp>

using namespace ftxui;
using namespace Arxiv;

namespace TextColors {
// Color scheme (Catppuccin Frappe)
const Color base = Color::RGB(48, 52, 70);          // #232634
const Color surface = Color::RGB(65, 69, 89);       // #303446
const Color text = Color::RGB(198, 208, 245);       // #c6d0f5
const Color subtext = Color::RGB(165, 173, 206);    // #a5adce
const Color primary = Color::RGB(133, 193, 220);    // #8caaee
const Color border = Color::RGB(153, 209, 219);     // #99d1db
const Color secondary = Color::RGB(244, 184, 228);  // #f4b8e4
const Color error = Color::RGB(231, 130, 132);      // #e78284
}

ArxivApp::ArxivApp(const Config& config)
    : core(config,
           std::make_unique<DatabaseManager>("articles.db"),
           std::make_unique<Fetcher>(config.get_topics(), config.get_download_dir()))
    , key_bindings(config.get_key_mappings())
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

void ArxivApp::UpdateVisibleRange() {
    auto articles = core.GetCurrentArticles();
    if(articles.empty()) return;

    int current_index = core.GetArticleIndex();
    
    // If current selection is outside visible range, update top_article_index
    if(current_index < top_article_index) {
        top_article_index = current_index;
    } else if(current_index >= top_article_index + visible_rows) {
        top_article_index = current_index - visible_rows + 1;
    }
}

void ArxivApp::ToggleHelp() {
    show_help = !show_help;
}

void ArxivApp::SetupUI() {
    filter_menu = Menu(&core.GetFilterOptions(), &core.GetFilterIndex())
        | CatchEvent([&](Event event) {
            if(key_bindings.matches(event, KeyBindings::Action::Next)) {
                core.SetFilterIndex(std::min(core.GetFilterIndex() + 1, 
                    static_cast<int>(core.GetFilterOptions().size()) - 1));
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::Previous)) {
                core.SetFilterIndex(std::max(core.GetFilterIndex() - 1, 0));
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::MoveRight)) {
                focused_pane = 1;
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::CreateProject)) {
                dialog_depth = 1;
                new_project_name.clear();
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::DeleteProject)) {
                if(core.GetFilterIndex() > 2) {
                    core.RemoveProject(core.GetFilterOptions()[static_cast<size_t>(core.GetFilterIndex())]);
                }
                return true;
            }
            return false;
        });
    
    article_list = Menu(&core.GetCurrentTitles(), &core.GetArticleIndex())
        | vscroll_indicator
        | frame
        | CatchEvent([&](Event event) {
            if(key_bindings.matches(event, KeyBindings::Action::Next)) {
                core.SetArticleIndex(std::min(core.GetArticleIndex() + 1, 
                    static_cast<int>(core.GetCurrentArticles().size()) - 1));
                title_start_position = 0;
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::Previous)) {
                core.SetArticleIndex(std::max(core.GetArticleIndex() - 1, 0));
                title_start_position = 0;
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::Bookmark)) {
                auto articles = core.GetCurrentArticles();
                if(!articles.empty()) {
                    core.ToggleBookmark(articles[static_cast<size_t>(core.GetArticleIndex())].link);
                }
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::CreateProject)) {
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
            if(key_bindings.matches(event, KeyBindings::Action::DownloadArticle)) {
                // Download the article pdf to an articles folder
                auto article = core.GetCurrentArticles()[static_cast<size_t>(core.GetArticleIndex())];
                spdlog::debug("[App]: Downloading article ({}) to articles folder", article.id());
                bool success = core.DownloadArticle(article.id());
                if(!success) {
                    dialog_depth = 3;
                    err_msg = fmt::format("Failed to download article: {}", article.id());
                }
                return true;
            }
            return false;
        });

    filter_pane = Renderer(filter_menu, [&] {
        return vbox({
            text("Filters") | (focused_pane == 0 ? inverted : bold) | color(TextColors::primary),
            separator() | color(TextColors::border),
            filter_menu->Render() | vscroll_indicator | frame | color(TextColors::text)
        }) | borderStyled(ROUNDED, TextColors::border) | bgcolor(TextColors::base);
    });

    article_pane = Renderer(article_list, [&] {
        auto articles = core.GetCurrentArticles();
        if(articles.empty()) {
            return vbox({
                text("Articles") | (focused_pane == 1 ? inverted : bold) | color(TextColors::primary),
                separator() | color(TextColors::border),
                text("No articles available") | center | color(TextColors::subtext),
                separator() | color(TextColors::border),
                text("Try changing filters.") | center | color(TextColors::subtext),
            }) | borderStyled(ROUNDED, TextColors::border) | bgcolor(TextColors::base);
        }

        // Calculate visible rows based on terminal size
        int filter_width = FilterPaneWidth();
        int remaining_width = Terminal::Size().dimx - filter_width - border_size; 
        int articles_width = show_detail ? remaining_width / 2 : remaining_width;
        visible_rows = Terminal::Size().dimy - 4;  // Account for header, separator, and borders

        // Update visible range
        UpdateVisibleRange();

        Elements menu_items;
        // Only render articles in the visible range
        for(size_t i = static_cast<size_t>(top_article_index); 
            i < std::min(static_cast<size_t>(top_article_index + visible_rows), articles.size()); 
            ++i) {
            const auto& article = articles[i];
            std::string title = article.title;
            
            if(i == static_cast<size_t>(core.GetArticleIndex())) {
                size_t start_pos = static_cast<size_t>(std::floor(title_start_position)) % article.title.length();
                if(title.length() > static_cast<size_t>(articles_width - 2)) {
                    title = title.substr(start_pos) + "    " + title.substr(0, start_pos);
                }
                title = "> " + title;
                menu_items.push_back(text(title) | bold | color(TextColors::primary));
            } else {
                title = "  " + title;
                menu_items.push_back(text(title) | color(TextColors::text));
            }
        }

        return vbox({
            text("Articles") | (focused_pane == 1 ? inverted : bold) | color(TextColors::primary),
            separator() | color(TextColors::border),
            vbox(menu_items) | vscroll_indicator | frame
        }) | borderStyled(ROUNDED, TextColors::border) | bgcolor(TextColors::base);
    });

    detail_view = Renderer([&] {
        auto articles = core.GetCurrentArticles();
        if(articles.empty() || core.GetArticleIndex() >= static_cast<int>(articles.size())) {
            return window(text("Detail View") | color(TextColors::primary), 
                         text("No details available.") | center | color(TextColors::subtext))
                         | bgcolor(TextColors::base);
        }
        
        const auto& article = articles[static_cast<size_t>(core.GetArticleIndex())];
        return vbox({
            text("Title: " + article.title) | bold | color(TextColors::primary),
            paragraph("Authors: " + article.authors) | color(TextColors::text),
            text("Link: " + article.link) | color(TextColors::secondary),
            separator() | color(TextColors::border),
            paragraph("Abstract: \n" + article.abstract) | color(TextColors::text),
        }) | borderStyled(ROUNDED, TextColors::border) | bgcolor(TextColors::base);
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
                text("Add to Projects") | bold | color(TextColors::primary),
                separator() | color(TextColors::border),
                text("No projects available. Create a project first.") | center | color(TextColors::subtext),
                separator() | color(TextColors::border),
                hbox({
                    text("Press Esc to close") | color(TextColors::subtext),
                }) | center,
            }) | borderStyled(ROUNDED, TextColors::border) | bgcolor(TextColors::surface) | clear_under | center;
        }

        std::vector<Element> menu_items;
        for(size_t i = 0; i < projects.size(); ++i) {
            const auto& project = projects[i];
            std::string prefix = checkbox_states[project] ? "[X] " : "[ ] ";
            auto item = text(prefix + project);
            if(i == static_cast<size_t>(selected_project_index)) {
                item |= inverted;
            }
            menu_items.push_back(item | color(TextColors::subtext));
        }

        return vbox({
            text("Add to Projects") | bold | color(TextColors::primary),
            separator() | color(TextColors::border),
            vbox(menu_items) | vscroll_indicator | frame,
            separator() | color(TextColors::border),
            hbox({
                text("Use j/k to navigate, Space to toggle, Enter to save, Esc to cancel") | color(TextColors::subtext),
            }) | center,
        }) | borderStyled(ROUNDED, TextColors::border) | bgcolor(TextColors::surface) | clear_under | center;
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

    // Create help dialog
    help_dialog = Renderer([&] {
        if (!show_help) return emptyElement();

        auto bindings = key_bindings.get_all_bindings();
        
        // Sort bindings by action name
        std::sort(bindings.begin(), bindings.end(),
                 [](const auto& a, const auto& b) { return a.first < b.first; });

        // Calculate terminal width and column widths
        int term_width = Terminal::Size().dimx;
        int dialog_width = std::min(term_width - 4, 80);  // Leave 2 chars margin on each side
        int column_width = (dialog_width - 4) / 3;  // 4 chars for separators between columns

        // Create three columns of bindings
        std::vector<Element> columns[3];
        for (size_t i = 0; i < bindings.size(); ++i) {
            const auto& [action, key] = bindings[i];
            columns[i % 3].push_back(
                hbox({
                    text(action) | bold | color(TextColors::primary),
                    text(": ") | color(TextColors::primary),
                    text(key) | color(TextColors::subtext)
                }) | size(WIDTH, EQUAL, column_width)
            );
        }

        // Create the dialog content
        std::vector<Element> dialog_content = {
            text("Key Bindings") | bold | center | color(TextColors::primary),
            separator() | color(TextColors::border),
        };

        // Add columns side by side
        for (size_t i = 0; i < columns[0].size(); ++i) {
            Elements row;
            for (int col = 0; col < 3; ++col) {
                if (i < columns[col].size()) {
                    row.push_back(columns[col][i]);
                } else {
                    row.push_back(text("") | size(WIDTH, EQUAL, column_width));
                }
                if (col < 2) row.push_back(text(" | ") | color(TextColors::border));
            }
            dialog_content.push_back(hbox(row));
        }

        dialog_content.push_back(separator() | color(TextColors::border));
        dialog_content.push_back(
            hbox({
                text("Press ") | color(TextColors::subtext),
                text(key_bindings.get_key(KeyBindings::Action::ShowHelp)) | bold | color(TextColors::primary),
                text(" to close") | color(TextColors::subtext),
            }) | center
        );

        return vbox(dialog_content) | borderStyled(ROUNDED, TextColors::border) | bgcolor(TextColors::surface) | clear_under | center;
    });

    // Add date range dialog renderer
    date_range_dialog = Renderer([&] {
        if (dialog_depth != 4) return emptyElement();
        
        std::string start_prompt = "Start date (YYYY-MM-DD): " + start_date;
        std::string end_prompt = "End date (YYYY-MM-DD): " + end_date;
        
        if (date_input_mode == DateInputMode::Start) {
            start_prompt = "> " + start_prompt;
        } else {
            end_prompt = "> " + end_prompt;
        }

        return vbox({
            text("Set Date Range") | bold | color(TextColors::primary),
            separator() | color(TextColors::border),
            text(start_prompt) | color(TextColors::text),
            text(end_prompt) | color(TextColors::text),
            separator() | color(TextColors::border),
            hbox({
                text("Use Tab to switch between dates, Enter to save, Esc to cancel") | color(TextColors::subtext),
            }) | center,
        }) | borderStyled(ROUNDED, TextColors::border) | bgcolor(TextColors::surface) | clear_under | center;
    });

    // Add search dialog renderer
    search_dialog = Renderer([&] {
        if (dialog_depth != 5) return emptyElement();
        
        std::vector<Element> elements = {
            text("Search Articles") | bold | color(TextColors::primary),
            separator() | color(TextColors::border),
        };

        // Query input
        if (selected_search_option == 0) {
            elements.push_back(text("> Query: " + search_query) | color(TextColors::primary));
        } else {
            elements.push_back(text("  Query: " + search_query) | color(TextColors::text));
        }

        elements.push_back(text("Search in:") | color(TextColors::text));

        // Search options
        elements.push_back(text("  [" + std::string(search_field == AppCore::SearchMode::title ? "X" : " ") + "] Title") | color(TextColors::text));
        elements.push_back(text("  [" + std::string(search_field == AppCore::SearchMode::authors ? "X" : " ") + "] Authors") | color(TextColors::text));
        elements.push_back(text("  [" + std::string(search_field == AppCore::SearchMode::abstract ? "X" : " ") + "] Abstract") | color(TextColors::text));

        elements.push_back(separator() | color(TextColors::border));
        elements.push_back(
            hbox({
                text("Use Tab to move, Space to toggle, Enter to search, Esc to cancel") | color(TextColors::subtext),
            }) | center
        );

        return vbox(elements) | borderStyled(ROUNDED, TextColors::border) | bgcolor(TextColors::surface) | clear_under | center;
    });

    main_renderer = Renderer(main_container, [&] {
        int filter_width = FilterPaneWidth();
        int remaining_width = Terminal::Size().dimx - filter_width - border_size; 

        int articles_width = show_detail ? remaining_width / 2 : remaining_width;
        int detail_width = show_detail ? remaining_width / 2 : 0;

        std::vector<Element> panes = {
            filter_pane->Render() | size(WIDTH, EQUAL, filter_width),
            article_pane->Render() | size(WIDTH, EQUAL, articles_width),
        };
        if(show_detail) {
            panes.push_back(detail_view->Render() | size(WIDTH, EQUAL, detail_width));
        }
        Element document = hbox(std::move(panes)) | bgcolor(TextColors::base);

        if (dialog_depth == 1) {
            auto new_project_dialog = vbox({
                text("New Project") | bold | color(TextColors::primary),
                separator() | color(TextColors::border),
                text("Enter project name: " + new_project_name) | color(TextColors::text),
                separator() | color(TextColors::border),
                hbox({
                    text("Press Enter to create, Esc to cancel") | color(TextColors::subtext),
                }) | center,
            }) | borderStyled(ROUNDED, TextColors::border) | bgcolor(TextColors::surface) | clear_under | center;

            document = dbox({
                document,
                new_project_dialog,
            });
        } else if (dialog_depth == 2) {
            document = dbox({
                document,
                project_dialog->Render(),
            });
        } else if (dialog_depth == 3) {
            auto error_dialog = vbox({
                text("ERROR") | bold | center | color(TextColors::error),
                separator() | color(TextColors::error),
                text(err_msg) | color(TextColors::text),
            }) | borderStyled(ROUNDED, TextColors::error) | bgcolor(TextColors::surface) | clear_under | center;

            document = dbox({
                document,
                error_dialog,
            });
        } else if (dialog_depth == 4) {
            document = dbox({
                document,
                date_range_dialog->Render(),
            });
        } else if (dialog_depth == 5) {
            document = dbox({
                document,
                search_dialog->Render(),
            });
        }

        // Add help dialog if active
        if (show_help) {
            document = dbox({
                document,
                help_dialog->Render(),
            });
        }

        return document;
    });

    event_handler = CatchEvent(main_renderer, [&](Event event) {
        // Handle help dialog first
        if (show_help) {
            if(key_bindings.matches(event, KeyBindings::Action::ShowHelp) || event == Event::Escape) {
                show_help = false;
                return true;
            }
            // Block all other events when help is shown
            return true;
        }

        // Handle help toggle
        if(key_bindings.matches(event, KeyBindings::Action::ShowHelp)) {
            ToggleHelp();
            return true;
        }

        // Handle date range dialog
        if (dialog_depth == 4) {
            if (event == Event::Return) {
                if (!start_date.empty() && !end_date.empty()) {
                    core.SetDateRange(start_date, end_date);
                }
                dialog_depth = 0;
                start_date.clear();
                end_date.clear();
                return true;
            }
            if (event.is_character()) {
                if (date_input_mode == DateInputMode::Start) {
                    start_date += event.character();
                } else {
                    end_date += event.character();
                }
                return true;
            }
            if (event == Event::Backspace) {
                if (date_input_mode == DateInputMode::Start) {
                    if (!start_date.empty()) {
                        start_date.pop_back();
                    }
                } else {
                    if (!end_date.empty()) {
                        end_date.pop_back();
                    }
                }
                return true;
            }
            if (event == Event::Tab) {
                date_input_mode = (date_input_mode == DateInputMode::Start) ? 
                    DateInputMode::End : DateInputMode::Start;
                return true;
            }
            if (event == Event::Escape) {
                dialog_depth = 0;
                start_date.clear();
                end_date.clear();
                return true;
            }
            return true;
        }

        // Handle date range key binding
        if (key_bindings.matches(event, KeyBindings::Action::SetDateRange) && 
            core.GetFilterIndex() == 3) {  // Range filter is selected
            dialog_depth = 4;
            date_input_mode = DateInputMode::Start;
            start_date.clear();
            end_date.clear();
            return true;
        }

        // Handle search dialog
        if (dialog_depth == 5) {
            if (event == Event::Return) {
                if (!search_query.empty()) {
                    core.SetSearchQuery(search_query, search_field == AppCore::SearchMode::title,
                                      search_field == AppCore::SearchMode::authors,
                                      search_field == AppCore::SearchMode::abstract);
                    core.SetFilterIndex(4);
                }
                dialog_depth = 0;
                search_query.clear();
                selected_search_option = 0;
                return true;
            }
            if (event.is_character() && selected_search_option == 0) {
                search_query += event.character();
                return true;
            }
            if (event == Event::Backspace && selected_search_option == 0) {
                if (!search_query.empty()) {
                    search_query.pop_back();
                }
                return true;
            }
            if (event == Event::Tab) {
                selected_search_option = (selected_search_option + 1) % 4;  // Cycle through 0-3
                return true;
            }
            if (event == Event::Character(' ')) {
                if (selected_search_option > 0) {
                    // Set the search field based on selected option
                    switch (selected_search_option) {
                        case 1: search_field = AppCore::SearchMode::title; break;
                        case 2: search_field = AppCore::SearchMode::authors; break;
                        case 3: search_field = AppCore::SearchMode::abstract; break;
                    }
                }
                return true;
            }
            if (event == Event::Escape) {
                dialog_depth = 0;
                search_query.clear();
                selected_search_option = 0;
                return true;
            }
            return true;
        }

        // Handle search key binding
        if (key_bindings.matches(event, KeyBindings::Action::Search)) {  // Search filter is selected
            dialog_depth = 5;
            search_query.clear();
            selected_search_option = 0;
            return true;
        }

        // Handle pane navigation
        if(key_bindings.matches(event, KeyBindings::Action::MoveLeft)) {
            focused_pane = 0;
            title_start_position = 0;
            return true;
        }
        if(key_bindings.matches(event, KeyBindings::Action::MoveRight)) {
            focused_pane = 1;
            return true;
        }

        // Rest of the event handling
        if(key_bindings.matches(event, KeyBindings::Action::ShowDetail)) {
            show_detail = !show_detail;
            return true;
        }
        if(key_bindings.matches(event, KeyBindings::Action::Quit) || event == Event::Escape) {
            if (dialog_depth > 0) {
                dialog_depth = 0;
                err_msg = "";
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
            if(key_bindings.matches(event, KeyBindings::Action::Next)) {
                auto projects = core.GetProjects();
                if(selected_project_index < static_cast<int>(projects.size()) - 1) {
                    selected_project_index++;
                }
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::Previous)) {
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
