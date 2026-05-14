#pragma once

#include "imgui.h"

#include <Arxiv/AppCore.hh>
#include <Arxiv/Article.hh>
#include <Arxiv/Config.hh>
#include <Arxiv/GuiStyle.hh>

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Exposed so tests can verify date formatting directly.
std::string format_date(const Arxiv::time_point &tp);

// Convert a GuiStyle::Color4 to ImVec4 without including imgui.h in GuiStyle.
inline ImVec4 to_imvec4(const GuiStyle::Color4 &c) {
    return {c[0], c[1], c[2], c[3]};
}

// Apply the ImGui global colour scheme that matches the style name.
// Called once at startup and again whenever the user changes the theme.
void apply_imgui_base_theme(const GuiStyle &style);

class ArxivGuiApp {
public:
    // quit_fn is called when the user requests quit (e.g. File→Quit). If
    // omitted (e.g. in tests) the action is silently ignored.
    explicit ArxivGuiApp(Arxiv::AppCore &core,
                         Arxiv::Config  &config,
                         std::function<void()> quit_fn = {});

    void render();

    // Settings actions — also invocable from tests.
    void open_settings();
    void apply_settings();
    void save_settings();

    // Returns the ImGuiKey bound to the named action, or ImGuiKey_None.
    ImGuiKey key_for(const std::string &action) const;

    // ---- Draft state (written by ImGui widgets; readable by tests) -------
    GuiStyle m_draft_style;
    char     m_draft_download_dir[512]{};
    int      m_draft_auto_refresh{0};
    float    m_draft_threshold{3.5f};
    int      m_draft_retrain{5};
    char     m_draft_keywords[256]{};
    char     m_draft_obsidian[512]{};
    char     m_draft_ranker[256]{};
    std::vector<std::string>               m_draft_topics;
    char                                   m_draft_new_topic[64]{};
    std::vector<Arxiv::Config::KeyMapping> m_draft_keys;
    std::string                            m_capturing_action;

private:
    Arxiv::AppCore       &m_core;
    Arxiv::Config        &m_config;
    std::function<void()> m_quit;

    GuiStyle m_style;
    std::unordered_map<std::string, ImGuiKey> m_key_map;

    // ---- Settings panel state -------------------------------------------
    bool m_show_settings{false};
    int  m_settings_tab{0};

    // ---- Status bar state -----------------------------------------------
    char  m_status_flash[64]{};
    float m_status_flash_timer{0.0f};

    // ---- Render methods -------------------------------------------------
    void render_menu_bar();
    void render_filter_panel(float width, float height);
    void render_article_panel(float width, float height);
    void render_detail_panel(float width, float height);
    void render_search_dialog();
    void render_settings_panel();
    void render_settings_appearance();
    void render_settings_articles();
    void render_settings_keybindings();
    void render_status_bar();

    // ---- Search dialog state --------------------------------------------
    bool m_show_search_dialog{false};
    char m_search_buf[512]{};
};
