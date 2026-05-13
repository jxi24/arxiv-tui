#include "ArxivGuiApp.hh"

#include "imgui.h"

#include <chrono>
#include <ctime>
#include <string>

std::string format_date(const Arxiv::time_point &tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

// ---------------------------------------------------------------------------
// ArxivGuiApp
// ---------------------------------------------------------------------------

ArxivGuiApp::ArxivGuiApp(Arxiv::AppCore &core, std::function<void()> quit_fn)
    : m_core(core), m_quit(std::move(quit_fn)) {}

void ArxivGuiApp::render() {
    const ImGuiIO &io = ImGui::GetIO();
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({W, H});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar();

    render_menu_bar();

    const float filter_w  = 200.0f;
    const float detail_w  = 420.0f;
    const float article_w = W - filter_w - detail_w;
    const float panel_h   = H - ImGui::GetFrameHeightWithSpacing() * 2.0f;

    render_filter_panel(filter_w, panel_h);
    ImGui::SameLine();
    render_article_panel(article_w, panel_h);
    ImGui::SameLine();
    render_detail_panel(detail_w, panel_h);

    ImGui::End();

    if (m_show_search_dialog) render_search_dialog();
}

// ---------------------------------------------------------------------------
void ArxivGuiApp::render_menu_bar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Refresh", "F5"))
            m_core.FetchArticles();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q") && m_quit)
            m_quit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        const auto &opts = m_core.GetFilterOptions();
        for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
            bool selected = (m_core.GetFilterIndex() == i);
            if (ImGui::MenuItem(opts[static_cast<size_t>(i)].c_str(), nullptr, selected))
                m_core.SetFilterIndex(i);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Search")) {
        if (ImGui::MenuItem("Find…", "/"))
            m_show_search_dialog = true;
        if (ImGui::MenuItem("Clear search"))
            m_core.ClearSearch();
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
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

    const float row_h = ImGui::GetTextLineHeightWithSpacing() * 2.2f;
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
        ImGui::TextColored(a.bookmarked ? ImVec4{1, 0.8f, 0.2f, 1} : ImVec4{0.5f, 0.5f, 0.5f, 1},
                           "%s", bm);
        ImGui::SameLine();

        std::string title = a.title;
        if (title.size() > 80) title = title.substr(0, 77) + "...";
        ImGui::TextUnformatted(title.c_str());

        ImGui::SetCursorScreenPos({item_pos.x + 4 + 28,
                                   item_pos.y + ImGui::GetTextLineHeightWithSpacing() + 2});
        ImGui::TextDisabled("%s  |  %s", a.authors.c_str(), format_date(a.date).c_str());

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
        if (ImGui::IsKeyPressed(ImGuiKey_J) && m_core.GetArticleIndex() < n - 1)
            m_core.SetArticleIndex(m_core.GetArticleIndex() + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_K) && m_core.GetArticleIndex() > 0)
            m_core.SetArticleIndex(m_core.GetArticleIndex() - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_B) && !articles.empty())
            m_core.ToggleBookmark(articles[static_cast<size_t>(m_core.GetArticleIndex())].link);
        if (ImGui::IsKeyPressed(ImGuiKey_Slash))
            m_show_search_dialog = true;
    }
}

// ---------------------------------------------------------------------------
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
    ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "%s", a.title.c_str());
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    ImGui::TextDisabled("Authors:");
    ImGui::SameLine();
    ImGui::PushTextWrapPos(width - 12);
    ImGui::TextUnformatted(a.authors.c_str());
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    ImGui::TextDisabled("Date: %s", format_date(a.date).c_str());
    if (!a.category.empty())
        ImGui::TextDisabled("Category: %s", a.category.c_str());

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Abstract:");
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

// ---------------------------------------------------------------------------
void ArxivGuiApp::render_search_dialog() {
    ImGui::OpenPopup("Search##dlg");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({440, 0});

    if (ImGui::BeginPopupModal("Search##dlg", &m_show_search_dialog,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Search query:");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        bool submit = ImGui::InputText("##q", m_search_buf, sizeof(m_search_buf),
                                       ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        if (submit || ImGui::Button("Search", {120, 0})) {
            if (m_search_buf[0] != '\0') {
                m_core.SetSearchQuery(m_search_buf, true, true, true);
                m_core.SetFilterIndex(Arxiv::AppCore::FilterView::Search);
            }
            m_show_search_dialog = false;
            m_search_buf[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) {
            m_show_search_dialog = false;
            m_search_buf[0] = '\0';
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_show_search_dialog = false;
            m_search_buf[0] = '\0';
        }
        ImGui::EndPopup();
    }
}
