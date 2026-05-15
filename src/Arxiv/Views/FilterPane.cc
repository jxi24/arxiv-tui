#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupFilterPane() {
    filter_menu = Menu(&core.GetFilterOptions(), &core.GetFilterIndex())
        | CatchEvent([&](Event event) {
            if(key_bindings.matches(event, KeyBindings::Action::Next)) {
                int idx = std::min(core.GetFilterIndex() + 1,
                    static_cast<int>(core.GetFilterOptions().size()) - 1);
                core.SetFilterIndex(idx);
                if (m_recorder) m_recorder->RecordSetFilterIndex(idx);
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::Previous)) {
                int idx = std::max(core.GetFilterIndex() - 1, 0);
                core.SetFilterIndex(idx);
                if (m_recorder) m_recorder->RecordSetFilterIndex(idx);
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::MoveRight)) {
                focused_pane = 1;
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::CreateProject)) {
                dialog_depth = Dialog::NewProject;
                new_project_name.clear();
                parent_for_new_project = (core.GetFilterView() == AppCore::FilterView::Project)
                    ? core.GetProjectNameForFilter(core.GetFilterIndex())
                    : "";
                return true;
            }
            if(key_bindings.matches(event, KeyBindings::Action::DeleteProject)) {
                if(core.GetFilterView() == AppCore::FilterView::Project) {
                    std::string proj = core.GetProjectNameForFilter(core.GetFilterIndex());
                    core.RemoveProject(proj);
                    if (m_recorder) m_recorder->RecordRemoveProject(proj);
                }
                return true;
            }
            return false;
        });

    filter_pane = Renderer(filter_menu, [&] {
        auto header = focused_pane == 0
            ? text(" Filters ") | bold | color(TextColors::base()) | bgcolor(TextColors::primary())
            : text(" Filters ") | bold | color(TextColors::primary());
        return vbox({
            header,
            separator() | color(TextColors::border()),
            filter_menu->Render() | vscroll_indicator | frame | color(TextColors::text())
        }) | borderStyled(ROUNDED, TextColors::border());
    });
}
