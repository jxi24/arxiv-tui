#include "ArxivGuiApp.hh"
#include "imgui.h"

void ArxivGuiApp::render_article_panel(float width, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
    ImGui::BeginChild("##articles", {width, height}, ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();

    const auto articles = m_core.GetCurrentArticles();
    ImGui::TextDisabled("%zu article(s)  —  %s",
                        articles.size(),
                        m_core.GetFilterOptions()[
                            static_cast<size_t>(m_core.GetFilterIndex())].c_str());
    ImGui::Separator();

    const float row_h = ImGui::GetTextLineHeightWithSpacing() * m_style.row_height_scale;
    for (int i = 0; i < static_cast<int>(articles.size()); ++i) {
        const auto &a = articles[static_cast<size_t>(i)];
        bool selected = (m_core.GetArticleIndex() == i);

        ImGui::PushID(i);
        if (ImGui::Selectable("##row", selected,
                              ImGuiSelectableFlags_None, {0, row_h}))
            m_core.SetArticleIndex(i);

        ImVec2 item_pos = ImGui::GetItemRectMin();
        ImGui::SetCursorScreenPos({item_pos.x + 4, item_pos.y + 2});

        const char *bm = a.bookmarked ? "[*] " : "    ";
        ImGui::TextColored(a.bookmarked ? to_imvec4(m_style.bookmark_color)
                                        : to_imvec4(m_style.disabled_color),
                           "%s", bm);
        ImGui::SameLine();

        std::string title = a.title;
        if (title.size() > 80) title = title.substr(0, 77) + "...";
        ImGui::TextUnformatted(title.c_str());

        ImGui::SetCursorScreenPos({item_pos.x + 4 + 28,
                                   item_pos.y + ImGui::GetTextLineHeightWithSpacing() + 2});
        ImGui::TextColored(to_imvec4(m_style.disabled_color),
                           "%s  |  %s", a.authors.c_str(), format_date(a.date).c_str());

        if (ImGui::BeginPopupContextItem("##ctx")) {
            m_core.SetArticleIndex(i);
            if (ImGui::MenuItem(a.bookmarked ? "Remove bookmark" : "Bookmark"))
                m_core.ToggleBookmark(a.link);
            if (ImGui::MenuItem("Download PDF"))
                m_core.DownloadArticle(a.id());
            if (ImGui::MenuItem("Copy link"))
                ImGui::SetClipboardText(a.link.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    ImGui::EndChild();

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        const auto n = static_cast<int>(articles.size());
        auto pressed = [](ImGuiKey k) { return k != ImGuiKey_None && ImGui::IsKeyPressed(k); };
        if (pressed(key_for("next")) && m_core.GetArticleIndex() < n - 1)
            m_core.SetArticleIndex(m_core.GetArticleIndex() + 1);
        if (pressed(key_for("previous")) && m_core.GetArticleIndex() > 0)
            m_core.SetArticleIndex(m_core.GetArticleIndex() - 1);
        if (pressed(key_for("bookmark")) && !articles.empty())
            m_core.ToggleBookmark(articles[static_cast<size_t>(m_core.GetArticleIndex())].link);
        if (pressed(key_for("search")))
            m_show_search_dialog = true;
        if (pressed(key_for("manage_projects")) && !articles.empty())
            open_project_dialog();
        if (pressed(key_for("settings")))
            open_settings();
    }
}
