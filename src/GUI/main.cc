// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Logging.hh"
#include "Arxiv/Paths.hh"
#include "Arxiv/Utf8.hh"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "spdlog/spdlog.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string format_date(const Arxiv::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

static void glfw_error_callback(int error, const char* description) {
    spdlog::error("GLFW error {}: {}", error, description);
}

// ---------------------------------------------------------------------------
// ArxivGuiApp
// ---------------------------------------------------------------------------

class ArxivGuiApp {
  public:
    explicit ArxivGuiApp(Arxiv::AppCore& core)
        : m_core(core) {}

    void render() {
        // Results of the background fetch, refresh and downloads.
        m_core.TryRefetchIfNeeded();
        mark_focused_read();

        const ImGuiIO& io = ImGui::GetIO();
        const float W = io.DisplaySize.x;
        const float H = io.DisplaySize.y;

        // Full-screen dockspace window with no decoration.
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({W, H});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::Begin("##root",
                     nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar();

        render_menu_bar();

        // Three-column layout: filter list | article list | detail
        const float filter_w = 200.0f;
        const float detail_w = 420.0f;
        const float article_w = W - filter_w - detail_w;
        const float panel_h = H - ImGui::GetFrameHeightWithSpacing() * 2.0f;

        render_filter_panel(filter_w, panel_h);
        ImGui::SameLine();
        render_article_panel(article_w, panel_h);
        ImGui::SameLine();
        render_detail_panel(detail_w, panel_h);
        render_status_bar();

        ImGui::End();

        if (m_show_search_dialog)
            render_search_dialog();
    }

  private:
    Arxiv::AppCore& m_core;

    // UI state
    bool m_show_search_dialog{false};
    char m_search_buf[512]{};
    std::string m_status_msg;
    std::string m_last_focused; // marked read when it changes

    const Arxiv::Article* focused(const std::vector<Arxiv::Article>& articles) const {
        const int idx = m_core.GetArticleIndex();
        if (idx < 0 || idx >= static_cast<int>(articles.size()))
            return nullptr;
        return &articles[static_cast<std::size_t>(idx)];
    }

    // Opening a paper reads it, as in the TUI's detail pane.
    void mark_focused_read() {
        const auto& articles = m_core.GetCurrentArticles();
        const auto* a = focused(articles);
        if (!a || a->link == m_last_focused)
            return;
        m_last_focused = a->link;
        if (!a->read)
            m_core.MarkArticleRead(a->link);
    }

    void download(const Arxiv::Article& a) {
        // A background task: the window keeps drawing while the PDF arrives.
        if (!m_core.StartDownloadArticle(a.link))
            m_status_msg = "Busy: " + m_core.GetBackgroundTaskStatus().label;
    }

    // Tags and projects for the focused paper (context menu and detail pane).
    void tag_and_project_menus(const Arxiv::Article& a) {
        if (ImGui::BeginMenu("Tags")) {
            const auto mine = m_core.GetTagsForArticle(a.link);
            for (const auto& tag : m_core.GetTags()) {
                const bool on = std::find(mine.begin(), mine.end(), tag) != mine.end();
                if (ImGui::MenuItem(("#" + tag).c_str(), nullptr, on)) {
                    if (on)
                        m_core.UnlinkArticleFromTag(a.link, tag);
                    else
                        m_core.LinkArticleToTag(a.link, tag);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Projects")) {
            const auto mine = m_core.GetProjectsForArticle(a.link);
            for (const auto& project : m_core.GetProjects()) {
                const bool in = std::find(mine.begin(), mine.end(), project) != mine.end();
                if (ImGui::MenuItem(project.c_str(), nullptr, in)) {
                    if (in)
                        m_core.UnlinkArticleFromProject(a.link, project);
                    else
                        m_core.LinkArticleToProject(a.link, project);
                }
            }
            ImGui::EndMenu();
        }
    }

    void render_status_bar() {
        std::string status;
        if (m_core.IsFetching())
            status = "Fetching new articles...";
        else if (!m_core.LastFetchError().empty())
            status = "Offline: showing stored papers";
        const auto task = m_core.GetBackgroundTaskStatus();
        if (task.running)
            status += (status.empty() ? "" : "   ") + task.label + "...";
        else if (!task.message.empty())
            status += (status.empty() ? "" : "   ") + task.message;
        if (!m_status_msg.empty())
            status += (status.empty() ? "" : "   ") + m_status_msg;
        ImGui::TextDisabled("%s", status.c_str());
    }

    // ---------------------------------------------------------------------------
    void render_menu_bar() {
        if (!ImGui::BeginMenuBar())
            return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Refresh", "F5", false, !m_core.IsFetching()))
                m_core.StartRefresh(); // from arXiv, in the background
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            const auto& opts = m_core.GetFilterOptions();
            for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
                bool selected = (m_core.GetFilterIndex() == i);
                if (ImGui::MenuItem(opts[static_cast<size_t>(i)].c_str(), nullptr, selected)) {
                    m_core.SetFilterIndex(i);
                }
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
    void render_filter_panel(float width, float height) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
        ImGui::BeginChild("##filters", {width, height}, ImGuiChildFlags_Borders);
        ImGui::PopStyleVar();

        ImGui::TextDisabled("Filters");
        ImGui::Separator();

        const auto opts = m_core.GetFilterOptions();
        for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
            if (const auto section = m_core.GetFilterSection(i); !section.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", section.c_str());
            }
            std::string label = opts[static_cast<size_t>(i)];
            if (const auto count = m_core.GetFilterCount(i))
                label += "  (" + std::to_string(*count) + ")";
            bool selected = (m_core.GetFilterIndex() == i);
            ImGui::PushID(i);
            if (ImGui::Selectable(
                    label.c_str(), selected, ImGuiSelectableFlags_None, {width - 12.0f, 0}))
                m_core.SetFilterIndex(i);
            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    // ---------------------------------------------------------------------------
    void render_article_panel(float width, float height) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
        ImGui::BeginChild("##articles", {width, height}, ImGuiChildFlags_Borders);
        ImGui::PopStyleVar();

        const auto articles = m_core.GetCurrentArticles();

        ImGui::TextDisabled(
            "%zu article(s)  |  %s",
            articles.size(),
            m_core.GetFilterOptions()[static_cast<size_t>(m_core.GetFilterIndex())].c_str());
        ImGui::Separator();

        const float row_h = ImGui::GetTextLineHeightWithSpacing() * 2.2f;
        for (int i = 0; i < static_cast<int>(articles.size()); ++i) {
            const auto& a = articles[static_cast<size_t>(i)];
            bool selected = (m_core.GetArticleIndex() == i);

            ImGui::PushID(i);
            if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_None, {0, row_h})) {
                m_core.SetArticleIndex(i);
            }

            // Overlay text on the selectable
            ImVec2 item_pos = ImGui::GetItemRectMin();
            ImGui::SetCursorScreenPos({item_pos.x + 4, item_pos.y + 2});

            // Bookmark indicator
            const char* bm = a.bookmarked ? "[*] " : "    ";
            ImGui::TextColored(
                a.bookmarked ? ImVec4{1, 0.8f, 0.2f, 1} : ImVec4{0.5f, 0.5f, 0.5f, 1}, "%s", bm);
            ImGui::SameLine();

            // Title (cut by character, not byte); read papers are dimmed.
            const std::string title =
                Arxiv::Utf8::Truncate(a.title.empty() ? "(no title)" : a.title, 80);
            if (a.read)
                ImGui::TextDisabled("%s", title.c_str());
            else
                ImGui::TextUnformatted(title.c_str());

            // Second line: authors + date
            ImGui::SetCursorScreenPos(
                {item_pos.x + 4 + 28, item_pos.y + ImGui::GetTextLineHeightWithSpacing() + 2});
            ImGui::TextDisabled("%s  |  %s", a.authors.c_str(), format_date(a.date).c_str());

            // Context menu on right-click
            if (ImGui::BeginPopupContextItem("##ctx")) {
                m_core.SetArticleIndex(i);
                if (ImGui::MenuItem(a.bookmarked ? "Remove bookmark" : "Bookmark"))
                    m_core.ToggleBookmark(a.link);
                if (ImGui::MenuItem("Download PDF"))
                    download(a);
                if (ImGui::MenuItem("Copy link"))
                    ImGui::SetClipboardText(a.link.c_str());
                tag_and_project_menus(a);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        ImGui::EndChild();

        // Keyboard shortcuts in article panel scope
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
    void render_detail_panel(float width, float height) {
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

        const auto& a = articles[static_cast<size_t>(idx)];

        // Title
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

        // Rating: 1-5 stars, the same ratings that train the ranker.
        const int rating = m_core.GetArticleRating(a.link);
        ImGui::TextDisabled("Rating:");
        for (int star = 1; star <= 5; ++star) {
            ImGui::SameLine();
            ImGui::PushID(star);
            if (ImGui::SmallButton(star <= rating ? "*" : "."))
                m_core.RateArticle(a.link, star);
            ImGui::PopID();
        }
        const auto tags = m_core.GetTagsForArticle(a.link);
        const auto projects = m_core.GetProjectsForArticle(a.link);
        std::string tag_line, project_line;
        for (const auto& t : tags)
            tag_line += (tag_line.empty() ? "#" : ", #") + t;
        for (const auto& p : projects)
            project_line += (project_line.empty() ? "" : ", ") + p;
        if (!tag_line.empty())
            ImGui::TextDisabled("Tags: %s", tag_line.c_str());
        if (!project_line.empty())
            ImGui::TextDisabled("Projects: %s", project_line.c_str());
        ImGui::Spacing();

        if (ImGui::SmallButton(a.bookmarked ? "Remove bookmark" : "Bookmark"))
            m_core.ToggleBookmark(a.link);
        ImGui::SameLine();
        if (ImGui::SmallButton("Download PDF"))
            download(a);
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy link"))
            ImGui::SetClipboardText(a.link.c_str());

        ImGui::EndChild();
    }

    // ---------------------------------------------------------------------------
    void render_search_dialog() {
        ImGui::OpenPopup("Search##dlg");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({440, 0});

        if (ImGui::BeginPopupModal(
                "Search##dlg", &m_show_search_dialog, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Search query:");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            bool submit = ImGui::InputText(
                "##q", m_search_buf, sizeof(m_search_buf), ImGuiInputTextFlags_EnterReturnsTrue);

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
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int /*argc*/, char** /*argv*/) {
    // Same files as the TUI: config.yml, database, ranker and keywords from
    // the XDG / install-prefix layout, never the working directory.
    const Arxiv::Paths paths = Arxiv::Paths::Resolve();
    paths.CreateDirectories();

    try {
        spdlog::set_default_logger(
            Arxiv::MakeFileLogger("arxiv_gui", paths.state_dir / "arxiv_gui.log", false));
    } catch (...) {
    }
    spdlog::set_level(spdlog::level::info);

    Arxiv::Config config(paths.config_file.string());
    for (const auto& warning : config.get_warnings())
        spdlog::warn("config: {}", warning);
    paths.ApplyTo(config);

    auto db = std::make_unique<Arxiv::DatabaseManager>(config.get_db_file());
    auto fetcher = std::make_unique<Arxiv::Fetcher>(config.get_topics(), config.get_download_dir());
    // Async: the window opens at once and the list fills in when the fetch
    // is done (render() picks it up through TryRefetchIfNeeded).
    Arxiv::AppCore core(
        config, std::move(db), std::move(fetcher), Arxiv::AppCore::FetchMode::Async);
    core.ReloadKeywords();

    // GLFW / OpenGL
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        spdlog::critical("Failed to initialise GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "arXiv Browser", nullptr, nullptr);
    if (!window) {
        spdlog::critical("Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // ImGui saves window layout to "imgui.ini" in the working directory by
    // default; keep it with the rest of the state instead.
    const std::string imgui_ini = (paths.state_dir / "imgui.ini").string();
    io.IniFilename = imgui_ini.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ArxivGuiApp app(core);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.render();

        ImGui::Render();
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
