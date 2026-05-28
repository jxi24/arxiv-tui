// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include <chrono>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string format_date(const Arxiv::time_point &tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

static void glfw_error_callback(int error, const char *description) {
    spdlog::error("GLFW error {}: {}", error, description);
}

// ---------------------------------------------------------------------------
// ArxivGuiApp
// ---------------------------------------------------------------------------

class ArxivGuiApp {
public:
    explicit ArxivGuiApp(Arxiv::AppCore &core) : m_core(core) {}

    void render() {
        const ImGuiIO &io = ImGui::GetIO();
        const float W = io.DisplaySize.x;
        const float H = io.DisplaySize.y;

        // Full-screen dockspace window with no decoration.
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

        // Three-column layout: filter list | article list | detail
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

private:
    Arxiv::AppCore &m_core;

    // UI state
    bool        m_show_search_dialog{false};
    char        m_search_buf[512]{};
    std::string m_status_msg;

    // ---------------------------------------------------------------------------
    void render_menu_bar() {
        if (!ImGui::BeginMenuBar()) return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Refresh", "F5"))
                m_core.FetchArticles();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            const auto &opts = m_core.GetFilterOptions();
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

        const auto &opts = m_core.GetFilterOptions();
        for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
            bool selected = (m_core.GetFilterIndex() == i);
            if (ImGui::Selectable(opts[static_cast<size_t>(i)].c_str(), selected,
                                  ImGuiSelectableFlags_None, {width - 12.0f, 0})) {
                m_core.SetFilterIndex(i);
            }
        }

        ImGui::EndChild();
    }

    // ---------------------------------------------------------------------------
    void render_article_panel(float width, float height) {
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
                                  ImGuiSelectableFlags_None, {0, row_h})) {
                m_core.SetArticleIndex(i);
            }

            // Overlay text on the selectable
            ImVec2 item_pos = ImGui::GetItemRectMin();
            ImGui::SetCursorScreenPos({item_pos.x + 4, item_pos.y + 2});

            // Bookmark indicator
            const char *bm = a.bookmarked ? "[*] " : "    ";
            ImGui::TextColored(a.bookmarked ? ImVec4{1, 0.8f, 0.2f, 1} : ImVec4{0.5f, 0.5f, 0.5f, 1},
                               "%s", bm);
            ImGui::SameLine();

            // Title (truncated)
            std::string title = a.title;
            if (title.size() > 80) title = title.substr(0, 77) + "...";
            ImGui::TextUnformatted(title.c_str());

            // Second line: authors + date
            ImGui::SetCursorScreenPos({item_pos.x + 4 + 28, item_pos.y + ImGui::GetTextLineHeightWithSpacing() + 2});
            ImGui::TextDisabled("%s  |  %s", a.authors.c_str(), format_date(a.date).c_str());

            // Context menu on right-click
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

        const auto &a = articles[static_cast<size_t>(idx)];

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
    void render_search_dialog() {
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
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int /*argc*/, char ** /*argv*/) {
    // Logging
    try {
        auto logger = spdlog::basic_logger_mt("arxiv_gui", "arxiv_gui.log");
        spdlog::set_default_logger(logger);
    } catch (...) {}
    spdlog::set_level(spdlog::level::info);

    // AppCore setup
    Arxiv::Config config;
    try {
        config = Arxiv::Config(".arxiv-tui.yml");
    } catch (...) {
        spdlog::warn("Config file not found, using defaults");
    }

    auto db      = std::make_unique<Arxiv::DatabaseManager>("articles.db");
    auto fetcher = std::make_unique<Arxiv::Fetcher>(config.get_topics(),
                                                     config.get_download_dir());
    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    // GLFW / OpenGL
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        spdlog::critical("Failed to initialise GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(1280, 800, "arXiv Browser", nullptr, nullptr);
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
    ImGuiIO &io = ImGui::GetIO();
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
