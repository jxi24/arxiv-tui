// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Logging.hh"
#include "Arxiv/Paths.hh"
#include "Arxiv/Process.hh"
#include "Arxiv/Utf8.hh"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Style.hh"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "spdlog/spdlog.h"

namespace {

using namespace ArxivGui;

std::string format_date(const Arxiv::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm); // the arXiv day, as in the TUI
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d %b %Y", &tm);
    return buf;
}

void glfw_error_callback(int error, const char* description) {
    spdlog::error("GLFW error {}: {}", error, description);
}

// The longest prefix of `text` that fits in `width` pixels with "…".
std::string fit(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return text;
    const auto starts = Arxiv::Utf8::CharStarts(text);
    std::size_t lo = 0, hi = starts.size();
    while (lo + 1 < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const std::string candidate = text.substr(0, starts[mid]) + "\xE2\x80\xA6";
        if (ImGui::CalcTextSize(candidate.c_str()).x <= width)
            lo = mid;
        else
            hi = mid;
    }
    return text.substr(0, starts[lo]) + "\xE2\x80\xA6";
}

// A five-pointed star centred at `c` (drawn, so it needs no font glyph).
void draw_star(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool filled) {
    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        const float radius = (i % 2 == 0) ? r : r * 0.45f;
        const float angle = -1.5707963f + static_cast<float>(i) * 0.6283185f;
        pts[i] = {c.x + std::cos(angle) * radius, c.y + std::sin(angle) * radius};
    }
    if (filled)
        dl->AddConcavePolyFilled(pts, 10, col);
    else
        dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, 1.5f);
}

// A rounded pill with text; returns its width.
float chip(ImDrawList* dl, ImVec2 pos, const std::string& label, ImU32 bg, ImU32 fg, ImFont* font) {
    ImGui::PushFont(font);
    const ImVec2 size = ImGui::CalcTextSize(label.c_str());
    const ImVec2 pad = {8, 3};
    dl->AddRectFilled(pos, {pos.x + size.x + pad.x * 2, pos.y + size.y + pad.y * 2}, bg, 99.0f);
    dl->AddText({pos.x + pad.x, pos.y + pad.y}, fg, label.c_str());
    ImGui::PopFont();
    return size.x + pad.x * 2;
}

// ---------------------------------------------------------------------------

class ArxivGuiApp {
  public:
    ArxivGuiApp(Arxiv::AppCore& core, Fonts fonts)
        : m_core(core)
        , m_fonts(fonts) {}

    void render() {
        // Results of the background fetch, refresh and downloads.
        m_core.TryRefetchIfNeeded();
        mark_focused_read();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        ImGui::Begin("##root",
                     nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);

        const float W = vp->WorkSize.x;
        const float H = vp->WorkSize.y;
        const float header_h = 60.0f;
        const float status_h = 30.0f;
        const float sidebar_w = 240.0f;
        const float detail_w = std::max(380.0f, W * 0.36f);
        const float body_h = H - header_h - status_h;

        render_header(W, header_h);
        ImGui::SetCursorPos({0, header_h});
        render_sidebar(sidebar_w, body_h);
        ImGui::SetCursorPos({sidebar_w, header_h});
        render_articles(W - sidebar_w - detail_w, body_h);
        ImGui::SetCursorPos({W - detail_w, header_h});
        render_detail(detail_w, body_h);
        ImGui::SetCursorPos({0, H - status_h});
        render_status_bar(W, status_h);

        handle_keys();
        ImGui::End();
    }

  private:
    Arxiv::AppCore& m_core;
    Fonts m_fonts;
    char m_search_buf[512]{};
    bool m_focus_search{false};
    bool m_scroll_to_focus{false};
    std::string m_status_msg;
    std::string m_last_focused; // marked read when it changes

    static constexpr float kCardHeight = 84.0f;
    static constexpr float kCardGap = 8.0f;

    const Arxiv::Article* focused() const {
        const auto& articles = m_core.GetCurrentArticles();
        const int idx = m_core.GetArticleIndex();
        if (idx < 0 || idx >= static_cast<int>(articles.size()))
            return nullptr;
        return &articles[static_cast<std::size_t>(idx)];
    }

    // Opening a paper reads it, as in the TUI's detail pane.
    void mark_focused_read() {
        const auto* a = focused();
        if (!a || a->link == m_last_focused)
            return;
        m_last_focused = a->link;
        if (!a->read)
            m_core.MarkArticleRead(a->link);
    }

    void select_article(int index) {
        m_core.SetArticleIndex(index);
        m_scroll_to_focus = true;
    }

    void download(const Arxiv::Article& a) {
        // A background task: the window keeps drawing while the PDF arrives.
        if (!m_core.StartDownloadArticle(a.link))
            m_status_msg = "Busy: " + m_core.GetBackgroundTaskStatus().label;
    }

    void open_in_browser() {
        for (const auto& link : m_core.GetLinksToOpen()) // arXiv links only
            Arxiv::Process::SpawnDetached({"xdg-open", link});
    }

    // ---------------------------------------------------------------------
    void render_header(float width, float height) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(origin, {origin.x + width, origin.y + height}, Palette::kSurface);
        dl->AddLine({origin.x, origin.y + height - 1},
                    {origin.x + width, origin.y + height - 1},
                    Palette::kBorder);

        // Brand and current view.
        ImGui::PushFont(m_fonts.title);
        const float title_y = origin.y + (height - ImGui::GetFontSize()) / 2;
        dl->AddText({origin.x + 20, title_y}, Palette::kAccent, "arXiv");
        const float brand_w = ImGui::CalcTextSize("arXiv").x;
        ImGui::PopFont();
        ImGui::PushFont(m_fonts.bold);
        const auto& opts = m_core.GetFilterOptions();
        const int fi = m_core.GetFilterIndex();
        std::string view = fi >= 0 && fi < static_cast<int>(opts.size())
                               ? opts[static_cast<std::size_t>(fi)]
                               : std::string();
        view.erase(0, view.find_first_not_of(' '));
        dl->AddText({origin.x + 32 + brand_w, origin.y + (height - ImGui::GetFontSize()) / 2},
                    Palette::kText,
                    view.c_str());
        ImGui::PopFont();

        // Search box and Refresh on the right.
        const float search_w = std::min(420.0f, width * 0.32f);
        const float refresh_w = 96.0f;
        const float frame_h = ImGui::GetFrameHeight();
        const float y = (height - frame_h) / 2;
        ImGui::SetCursorPos({width - search_w - refresh_w - 36, y});
        ImGui::SetNextItemWidth(search_w);
        if (m_focus_search) {
            ImGui::SetKeyboardFocusHere();
            m_focus_search = false;
        }
        if (ImGui::InputTextWithHint("##search",
                                     "Search titles, authors, abstracts   /",
                                     m_search_buf,
                                     sizeof(m_search_buf),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (m_search_buf[0] != '\0') {
                m_core.SetSearchQuery(m_search_buf, true, true, true);
                m_core.SetFilterIndex(Arxiv::AppCore::FilterView::Search);
            } else {
                m_core.ClearSearch();
            }
        }
        ImGui::SetCursorPos({width - refresh_w - 20, y});
        ImGui::BeginDisabled(m_core.IsFetching());
        if (ImGui::Button("Refresh", {refresh_w, 0}))
            m_core.StartRefresh(); // from arXiv, in the background
        ImGui::EndDisabled();
    }

    // ---------------------------------------------------------------------
    void render_sidebar(float width, float height) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12, 16});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 2});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::kSurface);
        ImGui::BeginChild("##sidebar", {width, height}, ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleColor();

        section_heading("VIEWS");
        const auto opts = m_core.GetFilterOptions();
        for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
            if (const auto section = m_core.GetFilterSection(i); !section.empty()) {
                ImGui::Dummy({0, 10});
                std::string upper = section;
                for (auto& ch : upper)
                    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                section_heading(upper.c_str());
            }
            nav_item(i, opts[static_cast<std::size_t>(i)], width - 24);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

    void section_heading(const char* text) {
        ImGui::PushFont(m_fonts.small);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
        ImGui::PushStyleColor(ImGuiCol_Text, Palette::kMuted);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy({0, 4});
    }

    void nav_item(int index, const std::string& label, float width) {
        const bool selected = m_core.GetFilterIndex() == index;
        const float h = 34.0f;
        ImGui::PushID(index);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##nav", {width, h}))
            m_core.SetFilterIndex(index);
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (selected || hovered)
            dl->AddRectFilled(pos,
                              {pos.x + width, pos.y + h},
                              selected ? Palette::kAccentSoft : Palette::kCardHover,
                              8.0f);
        // Sub-projects are indented with leading spaces in the option text.
        const auto indent = label.find_first_not_of(' ');
        const std::string text = indent == std::string::npos ? label : label.substr(indent);
        const float x =
            pos.x + 10 + static_cast<float>(indent == std::string::npos ? 0 : indent) * 6;
        ImGui::PushFont(selected ? m_fonts.bold : m_fonts.body);
        const float ty = pos.y + (h - ImGui::GetFontSize()) / 2;
        dl->AddText({x, ty}, selected ? Palette::kText : Palette::kMuted, text.c_str());
        ImGui::PopFont();
        if (const auto count = m_core.GetFilterCount(index); count && *count > 0) {
            ImGui::PushFont(m_fonts.small);
            const std::string n = std::to_string(*count);
            const float cw = ImGui::CalcTextSize(n.c_str()).x;
            ImGui::PopFont();
            chip(dl,
                 {pos.x + width - cw - 26, pos.y + (h - 20) / 2},
                 n,
                 selected ? Palette::kAccent : Palette::kCard,
                 selected ? Palette::kBackground : Palette::kMuted,
                 m_fonts.small);
        }
        ImGui::PopID();
    }

    // ---------------------------------------------------------------------
    void render_articles(float width, float height) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 16});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::kBackground);
        ImGui::BeginChild("##articles", {width, height}, ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        const auto& articles = m_core.GetCurrentArticles();
        if (articles.empty()) {
            empty_state(m_core.IsFetching() ? "Fetching papers..." : "No papers in this view");
            ImGui::EndChild();
            return;
        }

        // Keep the focused card in view after keyboard moves.
        const float row = kCardHeight + kCardGap;
        if (m_scroll_to_focus) {
            const float top = static_cast<float>(m_core.GetArticleIndex()) * row;
            const float view_h = ImGui::GetWindowHeight() - 32;
            if (top < ImGui::GetScrollY())
                ImGui::SetScrollY(top);
            else if (top + kCardHeight > ImGui::GetScrollY() + view_h)
                ImGui::SetScrollY(top + kCardHeight - view_h);
            m_scroll_to_focus = false;
        }

        const float card_w = ImGui::GetContentRegionAvail().x;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(articles.size()), row);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                article_card(i, articles[static_cast<std::size_t>(i)], card_w);
        clipper.End();
        ImGui::EndChild();
    }

    void empty_state(const char* text) {
        ImGui::PushFont(m_fonts.bold);
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos({(avail.x - size.x) / 2 + 16, avail.y / 2 - size.y});
        ImGui::PushStyleColor(ImGuiCol_Text, Palette::kMuted);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void article_card(int i, const Arxiv::Article& a, float width) {
        ImGui::PushID(i);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool selected = m_core.GetArticleIndex() == i;
        if (ImGui::InvisibleButton("##card", {width, kCardHeight}))
            select_article(i);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::BeginPopupContextItem("##ctx")) {
            m_core.SetArticleIndex(i);
            article_menu(a);
            ImGui::EndPopup();
        }
        ImGui::Dummy({0, kCardGap});

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 end = {pos.x + width, pos.y + kCardHeight};
        dl->AddRectFilled(pos,
                          end,
                          selected ? Palette::kCardHover
                                   : (hovered ? Palette::kCardHover : Palette::kCard),
                          12.0f);
        if (selected)
            dl->AddRect(pos, end, Palette::kAccent, 12.0f, 0, 1.5f);

        const float left = pos.x + 28;
        const float text_w = width - 28 - 16;
        // Unread: an accent dot.
        if (!a.read)
            dl->AddCircleFilled({pos.x + 14, pos.y + 22}, 4.0f, Palette::kAccent);

        ImGui::PushFont(a.read ? m_fonts.body : m_fonts.bold);
        const std::string title = fit(a.title.empty() ? "(no title)" : a.title, text_w - 24);
        dl->AddText({left, pos.y + 12}, a.read ? Palette::kMuted : Palette::kText, title.c_str());
        ImGui::PopFont();
        if (a.bookmarked)
            draw_star(dl, {pos.x + width - 20, pos.y + 21}, 7.0f, Palette::kStar, true);

        ImGui::PushFont(m_fonts.body);
        const std::string authors = fit(a.authors, text_w);
        dl->AddText({left, pos.y + 36}, Palette::kMuted, authors.c_str());
        ImGui::PopFont();

        float x = left;
        ImGui::PushFont(m_fonts.small);
        const std::string date = format_date(a.date);
        dl->AddText({x, pos.y + 61}, Palette::kMuted, date.c_str());
        x += ImGui::CalcTextSize(date.c_str()).x + 10;
        ImGui::PopFont();
        if (!a.category.empty())
            chip(dl,
                 {x, pos.y + 58},
                 a.category,
                 Palette::kAccentSoft,
                 Palette::kAccent,
                 m_fonts.small);
        ImGui::PopID();
    }

    // Context menu and detail-pane popups: everything one paper can do.
    void article_menu(const Arxiv::Article& a) {
        if (ImGui::MenuItem(a.bookmarked ? "Remove bookmark" : "Bookmark", "B"))
            m_core.ToggleBookmark(a.link);
        if (ImGui::MenuItem("Download PDF"))
            download(a);
        if (ImGui::MenuItem("Open in browser", "O"))
            open_in_browser();
        if (ImGui::MenuItem("Copy link"))
            ImGui::SetClipboardText(a.link.c_str());
        ImGui::Separator();
        tag_menu(a);
        project_menu(a);
    }

    void tag_menu(const Arxiv::Article& a) {
        if (!ImGui::BeginMenu("Tags"))
            return;
        const auto mine = m_core.GetTagsForArticle(a.link);
        const auto all = m_core.GetTags();
        if (all.empty())
            ImGui::TextDisabled("No tags yet (create them in the TUI with #)");
        for (const auto& tag : all) {
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

    void project_menu(const Arxiv::Article& a) {
        if (!ImGui::BeginMenu("Projects"))
            return;
        const auto mine = m_core.GetProjectsForArticle(a.link);
        const auto all = m_core.GetProjects();
        if (all.empty())
            ImGui::TextDisabled("No projects yet");
        for (const auto& project : all) {
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

    // ---------------------------------------------------------------------
    void render_detail(float width, float height) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {28, 24});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::kSurface);
        ImGui::BeginChild("##detail", {width, height}, ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        const auto* focused_article = focused();
        if (!focused_article) {
            empty_state("Select a paper");
            ImGui::EndChild();
            return;
        }
        const Arxiv::Article a = *focused_article; // actions below may reload the list
        const float wrap = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Chips: id, category, tags.
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float x = p.x;
            x += chip(dl,
                      {x, p.y},
                      "arXiv:" + a.id(),
                      Palette::kCard,
                      Palette::kMuted,
                      m_fonts.small) +
                 6;
            if (!a.category.empty())
                x += chip(dl,
                          {x, p.y},
                          a.category,
                          Palette::kAccentSoft,
                          Palette::kAccent,
                          m_fonts.small) +
                     6;
            for (const auto& tag : m_core.GetTagsForArticle(a.link)) {
                if (x > p.x + wrap - 60)
                    break;
                x += chip(dl, {x, p.y}, "#" + tag, Palette::kCard, Palette::kText, m_fonts.small) +
                     6;
            }
            ImGui::Dummy({0, 30});
        }

        ImGui::PushFont(m_fonts.title);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::TextUnformatted(a.title.empty() ? "(no title)" : a.title.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::Dummy({0, 4});

        ImGui::PushStyleColor(ImGuiCol_Text, Palette::kMuted);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::TextUnformatted(a.authors.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PushFont(m_fonts.small);
        ImGui::TextUnformatted(format_date(a.date).c_str());
        const auto projects = m_core.GetProjectsForArticle(a.link);
        if (!projects.empty()) {
            std::string line = "In ";
            for (std::size_t i = 0; i < projects.size(); ++i)
                line += (i ? ", " : "") + projects[i];
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::Dummy({0, 8});

        rating_row(a);
        ImGui::Dummy({0, 10});
        action_row(a);
        ImGui::Dummy({0, 14});

        ImGui::PushFont(m_fonts.small);
        ImGui::PushStyleColor(ImGuiCol_Text, Palette::kMuted);
        ImGui::TextUnformatted("ABSTRACT");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy({0, 2});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 6});
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::TextUnformatted(a.abstract.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleVar();

        ImGui::EndChild();
    }

    // Five drawn stars; click one to rate (the ratings train the ranker).
    void rating_row(const Arxiv::Article& a) {
        const int rating = m_core.GetArticleRating(a.link);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float size = 22.0f;
        int hovered_star = 0;
        ImVec2 centres[5];
        for (int star = 1; star <= 5; ++star) {
            ImGui::PushID(star);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            centres[star - 1] = {p.x + size / 2, p.y + size / 2};
            if (ImGui::InvisibleButton("##star", {size, size}))
                m_core.RateArticle(a.link, star);
            if (ImGui::IsItemHovered())
                hovered_star = star;
            ImGui::PopID();
            if (star < 5)
                ImGui::SameLine(0, 4);
        }
        // Draw after the buttons so a hover previews the rating.
        const int shown = hovered_star ? hovered_star : rating;
        for (int star = 1; star <= 5; ++star)
            draw_star(dl,
                      centres[star - 1],
                      size * 0.45f,
                      star <= shown ? Palette::kStar : Palette::kMuted,
                      star <= shown);
        ImGui::PushFont(m_fonts.small);
        ImGui::PushStyleColor(ImGuiCol_Text, Palette::kMuted);
        ImGui::TextUnformatted(rating ? "Your rating" : "Rate this paper");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void action_row(const Arxiv::Article& a) {
        // Primary action in the accent colour, the rest quiet.
        ImGui::PushStyleColor(ImGuiCol_Button, Palette::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Palette::rgb(146, 180, 250));
        ImGui::PushStyleColor(ImGuiCol_Text, Palette::kBackground);
        ImGui::PushFont(m_fonts.bold);
        if (ImGui::Button("Download PDF"))
            download(a);
        ImGui::PopFont();
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        if (ImGui::Button(a.bookmarked ? "Bookmarked" : "Bookmark"))
            m_core.ToggleBookmark(a.link);
        ImGui::SameLine();
        if (ImGui::Button("Open"))
            open_in_browser();
        ImGui::SameLine();
        if (ImGui::Button("More..."))
            ImGui::OpenPopup("##more");
        if (ImGui::BeginPopup("##more")) {
            if (ImGui::MenuItem("Copy link"))
                ImGui::SetClipboardText(a.link.c_str());
            tag_menu(a);
            project_menu(a);
            ImGui::EndPopup();
        }
    }

    // ---------------------------------------------------------------------
    void render_status_bar(float width, float height) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(origin, {origin.x + width, origin.y + height}, Palette::kSurface);
        dl->AddLine(origin, {origin.x + width, origin.y}, Palette::kBorder);

        ImU32 dot = Palette::kSuccess;
        std::string status = "Up to date";
        if (m_core.IsFetching()) {
            dot = Palette::kAccent;
            status = "Fetching new papers...";
        } else if (!m_core.LastFetchError().empty()) {
            dot = Palette::kDanger;
            status = "Offline: showing stored papers";
        }
        const auto task = m_core.GetBackgroundTaskStatus();
        if (task.running)
            status += "   " + task.label + "...";
        else if (!task.message.empty())
            status += "   " + task.message;
        if (!m_status_msg.empty())
            status += "   " + m_status_msg;

        ImGui::PushFont(m_fonts.small);
        const float y = origin.y + (height - ImGui::GetFontSize()) / 2;
        dl->AddCircleFilled({origin.x + 20, origin.y + height / 2}, 4.0f, dot);
        dl->AddText({origin.x + 32, y}, Palette::kMuted, status.c_str());
        const std::string count =
            std::to_string(m_core.GetCurrentArticles().size()) + " papers   j/k move   / search";
        const float cw = ImGui::CalcTextSize(count.c_str()).x;
        dl->AddText({origin.x + width - cw - 20, y}, Palette::kMuted, count.c_str());
        ImGui::PopFont();
    }

    // ---------------------------------------------------------------------
    void handle_keys() {
        if (ImGui::GetIO().WantTextInput)
            return; // typing in the search box
        const int n = static_cast<int>(m_core.GetCurrentArticles().size());
        const int idx = m_core.GetArticleIndex();
        if ((ImGui::IsKeyPressed(ImGuiKey_J) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) &&
            idx < n - 1)
            select_article(idx + 1);
        if ((ImGui::IsKeyPressed(ImGuiKey_K) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) && idx > 0)
            select_article(idx - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Slash))
            m_focus_search = true;
        if (const auto* a = focused()) {
            if (ImGui::IsKeyPressed(ImGuiKey_B))
                m_core.ToggleBookmark(a->link);
            if (ImGui::IsKeyPressed(ImGuiKey_O))
                open_in_browser();
        }
    }
};

} // namespace

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

    GLFWwindow* window = glfwCreateWindow(1440, 900, "arXiv", nullptr, nullptr);
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
    const Fonts fonts = LoadFonts();
    ApplyStyle();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ArxivGuiApp app(core, fonts);

    const ImVec4 clear = ImGui::ColorConvertU32ToFloat4(Palette::kBackground);
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
        glClearColor(clear.x, clear.y, clear.z, 1.0f);
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
