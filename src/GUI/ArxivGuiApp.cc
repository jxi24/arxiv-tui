#include "ArxivGuiApp.hh"

#include "imgui.h"

#include <Arxiv/Config.hh>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <string>

// ---------------------------------------------------------------------------
// Free helpers
// ---------------------------------------------------------------------------

std::string format_date(const Arxiv::time_point &tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

void apply_imgui_base_theme(const GuiStyle &style) {
    if (style.name == "Light") {
        ImGui::StyleColorsLight();
        return;
    }
    ImGui::StyleColorsDark();
    if (style.name == "Catppuccin Frappe") {
        // Override the Dark defaults with Frappe palette values.
        auto &c = ImGui::GetStyle().Colors;
        c[ImGuiCol_WindowBg]       = {0.188f, 0.204f, 0.275f, 1.0f}; // Base    #303446
        c[ImGuiCol_ChildBg]        = {0.165f, 0.180f, 0.247f, 1.0f}; // Mantle  #292c3c
        c[ImGuiCol_PopupBg]        = {0.188f, 0.204f, 0.275f, 1.0f};
        c[ImGuiCol_Border]         = {0.318f, 0.337f, 0.427f, 1.0f}; // Surface1 #51576d
        c[ImGuiCol_FrameBg]        = {0.255f, 0.271f, 0.349f, 1.0f}; // Surface0 #414559
        c[ImGuiCol_FrameBgHovered] = {0.318f, 0.337f, 0.427f, 1.0f};
        c[ImGuiCol_TitleBgActive]  = {0.141f, 0.153f, 0.216f, 1.0f}; // Crust   #232634
        c[ImGuiCol_Button]         = {0.318f, 0.337f, 0.427f, 1.0f};
        c[ImGuiCol_ButtonHovered]  = {0.400f, 0.420f, 0.514f, 1.0f};
        c[ImGuiCol_Header]         = {0.318f, 0.337f, 0.427f, 0.8f};
        c[ImGuiCol_HeaderHovered]  = {0.400f, 0.420f, 0.514f, 0.9f};
        c[ImGuiCol_HeaderActive]   = {0.549f, 0.667f, 0.933f, 1.0f}; // Blue   #8caaee
        c[ImGuiCol_Tab]            = {0.255f, 0.271f, 0.349f, 1.0f};
        c[ImGuiCol_TabHovered]     = {0.549f, 0.667f, 0.933f, 0.8f};
        c[ImGuiCol_TabActive]      = {0.318f, 0.337f, 0.427f, 1.0f};
        c[ImGuiCol_Text]           = {0.776f, 0.816f, 0.961f, 1.0f}; // Text   #c6d0f5
        c[ImGuiCol_TextDisabled]   = {0.647f, 0.678f, 0.808f, 1.0f}; // Sub0   #a5adce
        c[ImGuiCol_ScrollbarBg]    = {0.165f, 0.180f, 0.247f, 1.0f};
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ArxivGuiApp::ArxivGuiApp(Arxiv::AppCore &core,
                          Arxiv::Config  &config,
                          std::function<void()> quit_fn)
    : m_core(core)
    , m_config(config)
    , m_quit(std::move(quit_fn))
    , m_style(config.get_gui_style())
{
    apply_imgui_base_theme(m_style);
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void ArxivGuiApp::render() {
    const ImGuiIO &io = ImGui::GetIO();
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;

    const float status_h  = ImGui::GetFrameHeightWithSpacing();
    const float menubar_h = ImGui::GetFrameHeightWithSpacing();
    const float panel_h   = H - menubar_h - status_h;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({W, H});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar   | ImGuiWindowFlags_NoResize    |
                 ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar();

    render_menu_bar();

    const float filter_w  = m_style.filter_panel_width;
    const float detail_w  = m_style.detail_panel_width;
    const float article_w = W - filter_w - detail_w;

    render_filter_panel(filter_w, panel_h);
    ImGui::SameLine();
    render_article_panel(article_w, panel_h);
    ImGui::SameLine();
    render_detail_panel(detail_w, panel_h);

    render_status_bar();

    ImGui::End();

    if (m_show_search_dialog) render_search_dialog();
    if (m_show_settings)      render_settings_panel();
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void ArxivGuiApp::render_menu_bar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Refresh", "F5"))
            m_core.FetchArticles();
        if (ImGui::MenuItem("Settings", "Ctrl+,"))
            open_settings();
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
// Filter panel
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
// Article panel
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

        // Bookmark indicator using style colors
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
        if (ImGui::IsKeyPressed(ImGuiKey_J) && m_core.GetArticleIndex() < n - 1)
            m_core.SetArticleIndex(m_core.GetArticleIndex() + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_K) && m_core.GetArticleIndex() > 0)
            m_core.SetArticleIndex(m_core.GetArticleIndex() - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_B) && !articles.empty())
            m_core.ToggleBookmark(articles[static_cast<size_t>(m_core.GetArticleIndex())].link);
        if (ImGui::IsKeyPressed(ImGuiKey_Slash))
            m_show_search_dialog = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Comma) && ImGui::GetIO().KeyCtrl)
            open_settings();
    }
}

// ---------------------------------------------------------------------------
// Detail panel
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

// ---------------------------------------------------------------------------
// Search dialog
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
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
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

// ---------------------------------------------------------------------------
// Settings helpers
// ---------------------------------------------------------------------------

void ArxivGuiApp::open_settings() {
    m_draft_style = m_style;

    const auto &c = m_config;
    std::strncpy(m_draft_download_dir, c.get_download_dir().c_str(), 511);
    std::strncpy(m_draft_keywords,     c.get_keywords_file().c_str(), 255);
    std::strncpy(m_draft_obsidian,     c.get_obsidian_vault().c_str(), 511);
    std::strncpy(m_draft_ranker,       c.get_ranker_file().c_str(), 255);
    m_draft_auto_refresh = c.get_auto_refresh_minutes();
    m_draft_threshold    = c.get_recommend_threshold();
    m_draft_retrain      = c.get_retrain_interval();
    m_draft_topics       = c.get_topics();
    m_draft_keys         = c.get_key_mappings();
    m_draft_new_topic[0] = '\0';
    m_capturing_action.clear();
    m_settings_tab       = 0;
    m_show_settings      = true;
}

void ArxivGuiApp::apply_settings() {
    // Visual style
    m_style = m_draft_style;
    apply_imgui_base_theme(m_style);
    m_config.set_gui_style(m_style);

    // Article settings
    m_config.set_download_dir(m_draft_download_dir);
    m_config.set_auto_refresh_minutes(m_draft_auto_refresh);
    m_config.set_recommend_threshold(m_draft_threshold);
    m_config.set_retrain_interval(m_draft_retrain);
    m_config.set_keywords_file(m_draft_keywords);
    m_config.set_obsidian_vault(m_draft_obsidian);
    m_config.set_ranker_file(m_draft_ranker);
    m_config.set_topics(m_draft_topics);
    m_config.set_key_mappings(m_draft_keys);

    // Hot-reload AppCore
    m_core.SetRecommendThreshold(m_draft_threshold);

    const bool refresh_changed =
        m_draft_auto_refresh != m_config.get_auto_refresh_minutes();
    if (refresh_changed) {
        m_core.StopAutoRefresh();
        if (m_draft_auto_refresh > 0) m_core.StartAutoRefresh();
    }

    if (m_draft_topics != m_core.GetTopics()) {
        m_core.FetchArticles();
    }
}

void ArxivGuiApp::save_settings() {
    apply_settings();
    m_config.save();
    std::snprintf(m_status_flash, sizeof(m_status_flash), "Saved to %s",
                  m_config.get_config_file().c_str());
    m_status_flash_timer = 2.0f;
}

// ---------------------------------------------------------------------------
// Settings panel — shell + tab bar (content filled in later commits)
// ---------------------------------------------------------------------------

void ArxivGuiApp::render_settings_panel() {
    ImGui::OpenPopup("Settings##dlg");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({600, 520}, ImGuiCond_Always);

    if (!ImGui::BeginPopupModal("Settings##dlg", &m_show_settings,
                                ImGuiWindowFlags_NoResize)) return;

    if (ImGui::BeginTabBar("##stabs")) {
        if (ImGui::BeginTabItem("Appearance")) {
            m_settings_tab = 0;
            render_settings_appearance();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Articles")) {
            m_settings_tab = 1;
            render_settings_articles();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Key Bindings")) {
            m_settings_tab = 2;
            render_settings_keybindings();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Apply", {110, 0})) {
        apply_settings();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", {110, 0})) {
        save_settings();
        m_show_settings = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {110, 0})) {
        // Revert live style preview to the saved style
        m_style = m_config.get_gui_style();
        apply_imgui_base_theme(m_style);
        m_show_settings = false;
    }

    ImGui::EndPopup();
}

void ArxivGuiApp::render_settings_appearance() {
    static const char *kThemes[] = {"Dark", "Light", "Catppuccin Frappe", "Custom"};
    static const int   kNumBuiltin = 3;

    // Determine the current combo index.
    int theme_idx = kNumBuiltin; // "Custom" fallback
    for (int i = 0; i < kNumBuiltin; ++i) {
        if (m_draft_style.name == kThemes[i]) { theme_idx = i; break; }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Theme:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("##theme", &theme_idx, kThemes, IM_ARRAYSIZE(kThemes))) {
        if (theme_idx < kNumBuiltin) {
            m_draft_style = GuiStyle::from_name(kThemes[theme_idx]);
            // Live preview of the base theme
            apply_imgui_base_theme(m_draft_style);
        } else {
            m_draft_style.name = "Custom";
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Layout sliders
    ImGui::Text("Filter panel width:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("##fpw", &m_draft_style.filter_panel_width, 100.0f, 400.0f, "%.0f px");

    ImGui::Text("Detail panel width:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("##dpw", &m_draft_style.detail_panel_width, 200.0f, 700.0f, "%.0f px");

    ImGui::Text("Row height scale:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("##rhs", &m_draft_style.row_height_scale, 1.0f, 5.0f, "%.1f x");

    ImGui::Text("Font size:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("##fs", &m_draft_style.font_size, 8.0f, 32.0f, "%.0f pt");
    ImGui::SameLine();
    ImGui::TextDisabled("(restart to apply)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Per-role colours:");
    ImGui::Spacing();

    // Color pickers — always shown; enable custom name when any color is edited.
    auto color_edit = [&](const char *label, GuiStyle::Color4 &c) {
        float rgba[4] = {c[0], c[1], c[2], c[3]};
        ImGui::Text("%s", label);
        ImGui::SameLine(130);
        if (ImGui::ColorEdit4(("##" + std::string(label)).c_str(), rgba,
                              ImGuiColorEditFlags_NoInputs)) {
            c = {rgba[0], rgba[1], rgba[2], rgba[3]};
            m_draft_style.name = "Custom";
        }
    };
    color_edit("Title colour",    m_draft_style.title_color);
    color_edit("Bookmark colour", m_draft_style.bookmark_color);
    color_edit("Accent colour",   m_draft_style.accent_color);
    color_edit("Disabled colour", m_draft_style.disabled_color);
}

void ArxivGuiApp::render_settings_articles() {
    ImGui::Text("Download dir:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##dd", m_draft_download_dir, sizeof(m_draft_download_dir));

    ImGui::Text("Auto-refresh:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("##ar", &m_draft_auto_refresh);
    ImGui::SameLine();
    ImGui::TextDisabled("minutes (0 = off)");

    ImGui::Text("Score threshold:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("##thr", &m_draft_threshold, 0.0f, 10.0f, "%.1f");

    ImGui::Text("Retrain every:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("##ret", &m_draft_retrain);
    ImGui::SameLine();
    ImGui::TextDisabled("fetches");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Topics (arXiv category IDs):");
    ImGui::Spacing();

    for (int i = 0; i < static_cast<int>(m_draft_topics.size()); ++i) {
        ImGui::BulletText("%s", m_draft_topics[static_cast<size_t>(i)].c_str());
        ImGui::SameLine(250);
        ImGui::PushID(i);
        if (ImGui::SmallButton("Remove"))
            m_draft_topics.erase(m_draft_topics.begin() + i);
        ImGui::PopID();
    }

    ImGui::SetNextItemWidth(140);
    ImGui::InputText("##newtopic", m_draft_new_topic, sizeof(m_draft_new_topic));
    ImGui::SameLine();
    if (ImGui::Button("Add topic") && m_draft_new_topic[0] != '\0') {
        m_draft_topics.emplace_back(m_draft_new_topic);
        m_draft_new_topic[0] = '\0';
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Keywords file:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##kw", m_draft_keywords, sizeof(m_draft_keywords));

    ImGui::Text("Obsidian vault:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##obs", m_draft_obsidian, sizeof(m_draft_obsidian));

    ImGui::Text("Ranker file:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##rnk", m_draft_ranker, sizeof(m_draft_ranker));
}

void ArxivGuiApp::render_settings_keybindings() {
    constexpr float COL_ACTION = 250.0f;

    ImGui::TextDisabled("Click a binding to capture a new key.");
    ImGui::Spacing();

    // Detect any duplicate keys for conflict highlighting.
    auto has_conflict = [&](int idx) {
        const auto &k = m_draft_keys[static_cast<size_t>(idx)].key;
        for (int j = 0; j < static_cast<int>(m_draft_keys.size()); ++j) {
            if (j != idx && m_draft_keys[static_cast<size_t>(j)].key == k)
                return true;
        }
        return false;
    };

    if (ImGui::BeginTable("##kbtbl", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action",  ImGuiTableColumnFlags_WidthFixed, COL_ACTION);
        ImGui::TableSetupColumn("Key",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(m_draft_keys.size()); ++i) {
            auto &km = m_draft_keys[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(km.action.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(i);
            bool capturing = (m_capturing_action == km.action);
            bool conflict  = has_conflict(i);

            if (conflict)
                ImGui::PushStyleColor(ImGuiCol_Button, {0.8f, 0.2f, 0.2f, 1.0f});
            else if (capturing)
                ImGui::PushStyleColor(ImGuiCol_Button, {0.2f, 0.6f, 0.2f, 1.0f});

            const std::string label = capturing ? "[press key…]" : km.key;
            if (ImGui::Button(label.c_str(), {-1, 0})) {
                m_capturing_action = capturing ? "" : km.action;
            }

            if (conflict || capturing) ImGui::PopStyleColor();

            if (capturing) {
                // Scan all letters and digits for a key press.
                for (ImGuiKey key = ImGuiKey_A; key <= ImGuiKey_Z;
                     key = static_cast<ImGuiKey>(key + 1)) {
                    if (ImGui::IsKeyPressed(key, false)) {
                        const char letter[2] = {
                            static_cast<char>('a' + (key - ImGuiKey_A)), '\0'};
                        km.key = letter;
                        m_capturing_action.clear();
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    m_capturing_action.clear();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void ArxivGuiApp::render_status_bar() {
    const auto articles = m_core.GetCurrentArticles();
    const auto &opts    = m_core.GetFilterOptions();

    std::string status = std::to_string(articles.size()) + " article(s)";
    if (!opts.empty())
        status += "  |  " + opts[static_cast<size_t>(m_core.GetFilterIndex())];

    if (m_status_flash_timer > 0.0f) {
        const float dt = ImGui::GetIO().DeltaTime;
        m_status_flash_timer -= dt;
        const float alpha = std::min(1.0f, m_status_flash_timer);
        ImGui::TextColored({0.4f, 1.0f, 0.4f, alpha}, "%s", m_status_flash);
        ImGui::SameLine();
        ImGui::TextDisabled("  |  ");
        ImGui::SameLine();
    }

    ImGui::TextDisabled("%s", status.c_str());
}
