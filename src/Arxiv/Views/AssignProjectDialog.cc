#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupAssignProjectDialog() {
    project_checkbox_container = Container::Vertical({});

    project_dialog = Renderer([&] {
        if (dialog_depth != Dialog::AssignProject) return emptyElement();

        auto projects = core.GetProjects();
        if (projects.empty()) {
            return vbox({
                text("Add to Projects") | bold | color(TextColors::primary()),
                separator() | color(TextColors::border()),
                text("No projects available. Create a project first.") | center | color(TextColors::subtext()),
                separator() | color(TextColors::border()),
                hbox({
                    text("Press Esc to close") | color(TextColors::subtext()),
                }) | center,
            }) | borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) | clear_under | center;
        }

        std::vector<Element> menu_items;
        for(size_t i = 0; i < projects.size(); ++i) {
            const auto& project = projects[i];
            std::string prefix = checkbox_states[project] ? "[X] " : "[ ] ";
            auto item = text(prefix + project);
            if(i == static_cast<size_t>(selected_project_index)) {
                item |= inverted;
            }
            menu_items.push_back(item | color(TextColors::subtext()));
        }

        return vbox({
            text("Add to Projects") | bold | color(TextColors::primary()),
            separator() | color(TextColors::border()),
            vbox(menu_items) | vscroll_indicator | frame,
            separator() | color(TextColors::border()),
            hbox({
                text("Use j/k to navigate, Space to toggle, Enter to save, Esc to cancel") | color(TextColors::subtext()),
            }) | center,
        }) | borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) | clear_under | center;
    });

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
}

bool ArxivApp::HandleAssignProjectEvent(ftxui::Event event) {
    if (event == Event::Return) {
        auto articles = core.GetCurrentArticles();
        if (!articles.empty()) {
            auto article = articles[static_cast<size_t>(core.GetArticleIndex())];
            for(const auto& [project, selected] : checkbox_states) {
                if(selected) {
                    core.LinkArticleToProject(article.link, project);
                    if (m_recorder) m_recorder->RecordLinkArticleToProject(article.link, project);
                } else {
                    core.UnlinkArticleFromProject(article.link, project);
                    if (m_recorder) m_recorder->RecordUnlinkArticleFromProject(article.link, project);
                }
            }
        }
        dialog_depth = Dialog::None;
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
    return false;
}
