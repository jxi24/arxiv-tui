#include "ArxivGuiApp.hh"
#include "imgui.h"

void ArxivGuiApp::render_filter_panel(float width, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
    ImGui::BeginChild("##filters", {width, height}, ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();

    ImGui::TextDisabled("Filters");
    ImGui::Separator();

    const auto &opts = m_core.GetFilterOptions();
    const int project_start = static_cast<int>(Arxiv::AppCore::FilterView::Project);
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        bool selected = (m_core.GetFilterIndex() == i);
        ImGui::PushID(i);
        if (ImGui::Selectable(opts[static_cast<size_t>(i)].c_str(), selected,
                              ImGuiSelectableFlags_None, {width - 12.0f, 0}))
            m_core.SetFilterIndex(i);

        // Right-click on a project entry shows a context menu.
        if (i >= project_start && ImGui::BeginPopupContextItem("##fctx")) {
            const std::string proj_name = m_core.GetProjectNameForFilter(i);
            if (ImGui::MenuItem("Delete project")) {
                m_core.RemoveProject(proj_name);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // New-project controls below the list.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::SetNextItemWidth(width - 12.0f);
    bool add = ImGui::InputText("##fpnew", m_new_project_buf,
                                sizeof(m_new_project_buf),
                                ImGuiInputTextFlags_EnterReturnsTrue);
    if (add && m_new_project_buf[0] != '\0') {
        m_core.AddProject(m_new_project_buf);
        m_new_project_buf[0] = '\0';
    }
    if (ImGui::Button("New project", {width - 12.0f, 0}) && m_new_project_buf[0] != '\0') {
        m_core.AddProject(m_new_project_buf);
        m_new_project_buf[0] = '\0';
    }

    ImGui::EndChild();
}
