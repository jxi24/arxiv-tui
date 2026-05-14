#include "ArxivGuiApp.hh"
#include "imgui.h"

void ArxivGuiApp::render_filter_panel(float width, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
    ImGui::BeginChild("##filters", {width, height}, ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();

    ImGui::TextDisabled("Filters");
    ImGui::Separator();

    const auto &opts = m_core.GetFilterOptions();
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        bool selected = (m_core.GetFilterIndex() == i);
        if (ImGui::Selectable(opts[static_cast<size_t>(i)].c_str(), selected,
                              ImGuiSelectableFlags_None, {width - 12.0f, 0}))
            m_core.SetFilterIndex(i);
    }

    ImGui::EndChild();
}
