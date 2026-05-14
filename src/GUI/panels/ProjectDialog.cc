#include "ArxivGuiApp.hh"
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string>

void ArxivGuiApp::render_project_dialog() {
    const auto articles = m_core.GetCurrentArticles();
    const int idx = m_core.GetArticleIndex();
    if (articles.empty() || idx < 0 || idx >= static_cast<int>(articles.size())) {
        m_show_project_dialog = false;
        return;
    }
    const auto &a = articles[static_cast<size_t>(idx)];

    ImGui::OpenPopup("Manage Projects##dlg");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({520, 420}, ImGuiCond_Always);

    if (!ImGui::BeginPopupModal("Manage Projects##dlg", &m_show_project_dialog,
                                ImGuiWindowFlags_NoResize)) return;

    // Article title as header.
    ImGui::PushTextWrapPos(500);
    ImGui::TextColored(to_imvec4(m_style.title_color), "%s", a.title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    ImGui::Spacing();

    // Current membership — fetched fresh each frame so external changes show up.
    const auto membership = m_core.GetProjectsForArticle(a.link);
    const auto all_projects = m_core.GetProjects();

    if (all_projects.empty()) {
        ImGui::TextDisabled("No projects yet. Create one below.");
    } else {
        if (ImGui::BeginTable("##projtbl", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY,
                              {0, 220})) {
            ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("Note",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto &proj : all_projects) {
                const bool is_member =
                    std::find(membership.begin(), membership.end(), proj) != membership.end();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(proj.c_str());

                bool checked = is_member;
                if (ImGui::Checkbox(proj.c_str(), &checked)) {
                    if (checked)
                        m_core.LinkArticleToProject(a.link, proj);
                    else
                        m_core.UnlinkArticleFromProject(a.link, proj);
                }

                ImGui::TableSetColumnIndex(1);
                if (is_member) {
                    // Ensure we have a note buffer for this project.
                    if (m_proj_note_bufs.find(proj) == m_proj_note_bufs.end()) {
                        auto &buf = m_proj_note_bufs[proj];
                        buf.fill(0);
                        const std::string note = m_core.GetProjectNote(proj, a.link);
                        std::strncpy(buf.data(), note.c_str(), 255);
                    }
                    auto &buf = m_proj_note_bufs[proj];
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputText("##note", buf.data(), 256,
                                        ImGuiInputTextFlags_EnterReturnsTrue))
                        m_core.SetProjectNote(proj, a.link, buf.data());
                } else {
                    ImGui::TextDisabled("—");
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Create new project.
    ImGui::TextDisabled("New project:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    bool create = ImGui::InputText("##newproj", m_new_project_buf,
                                   sizeof(m_new_project_buf),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((create || ImGui::Button("Create")) && m_new_project_buf[0] != '\0') {
        m_core.AddProject(m_new_project_buf);
        m_new_project_buf[0] = '\0';
    }

    ImGui::Spacing();
    if (ImGui::Button("Close", {120, 0}))
        m_show_project_dialog = false;

    ImGui::EndPopup();
}
