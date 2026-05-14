#include "ArxivGuiApp.hh"
#include "imgui.h"

void ArxivGuiApp::render_detail_panel(float width, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});
    ImGui::BeginChild("##detail", {width, height}, ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();

    const auto articles = m_core.GetCurrentArticles();
    const int idx = m_core.GetArticleIndex();
    if (articles.empty() || idx < 0 || idx >= static_cast<int>(articles.size())) {
        ImGui::TextDisabled("No article selected.");
        ImGui::EndChild();
        return;
    }

    const auto &a = articles[static_cast<size_t>(idx)];

    ImGui::PushTextWrapPos(width - 12);
    ImGui::TextColored(to_imvec4(m_style.title_color), "%s", a.title.c_str());
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    ImGui::TextColored(to_imvec4(m_style.disabled_color), "Authors:");
    ImGui::SameLine();
    ImGui::PushTextWrapPos(width - 12);
    ImGui::TextUnformatted(a.authors.c_str());
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    ImGui::TextColored(to_imvec4(m_style.disabled_color),
                       "Date: %s", format_date(a.date).c_str());
    if (!a.category.empty())
        ImGui::TextColored(to_imvec4(m_style.disabled_color),
                           "Category: %s", a.category.c_str());

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(to_imvec4(m_style.disabled_color), "Abstract:");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(width - 12);
    ImGui::TextUnformatted(a.abstract.c_str());
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::SmallButton(a.bookmarked ? "Remove bookmark" : "Bookmark"))
        m_core.ToggleBookmark(a.link);
    ImGui::SameLine();
    if (ImGui::SmallButton("Download PDF"))
        m_core.DownloadArticle(a.id());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy link"))
        ImGui::SetClipboardText(a.link.c_str());

    ImGui::EndChild();
}
