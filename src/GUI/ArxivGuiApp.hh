#pragma once

#include "imgui.h"

#include <Arxiv/AppCore.hh>
#include <Arxiv/Article.hh>

#include <chrono>
#include <functional>
#include <string>

// Exposed so tests can verify date formatting directly.
std::string format_date(const Arxiv::time_point &tp);

class ArxivGuiApp {
public:
    // quit_fn is called when the user requests quit (e.g. File→Quit). If
    // omitted (e.g. in tests) the action is silently ignored.
    explicit ArxivGuiApp(Arxiv::AppCore &core,
                         std::function<void()> quit_fn = {});

    void render();

private:
    Arxiv::AppCore        &m_core;
    std::function<void()>  m_quit;

    bool m_show_search_dialog{false};
    char m_search_buf[512]{};
    std::string m_status_msg;

    void render_menu_bar();
    void render_filter_panel(float width, float height);
    void render_article_panel(float width, float height);
    void render_detail_panel(float width, float height);
    void render_search_dialog();
};
