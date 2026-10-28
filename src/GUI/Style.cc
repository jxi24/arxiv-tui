// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Style.hh"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

namespace ArxivGui {

namespace {

// Regular / semibold pairs, best first. Paths cover Debian/Ubuntu, Fedora
// and Arch layouts.
struct FontPair {
    const char* regular;
    const char* bold;
};
const std::vector<FontPair> kCandidates = {
    {"/usr/share/fonts/opentype/inter/Inter-Regular.otf",
     "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"},
    {"/usr/share/fonts/inter/Inter-Regular.otf", "/usr/share/fonts/inter/Inter-SemiBold.otf"},
    {"/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
     "/usr/share/fonts/truetype/inter/Inter-SemiBold.ttf"},
    {"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
     "/usr/share/fonts/truetype/noto/NotoSans-SemiBold.ttf"},
    {"/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-SemiBold.ttf"},
    {"/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
     "/usr/share/fonts/google-noto/NotoSans-SemiBold.ttf"},
    {"/usr/share/fonts/opentype/cantarell/Cantarell-Regular.otf",
     "/usr/share/fonts/opentype/cantarell/Cantarell-Bold.otf"},
    {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"},
    {"/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"},
};

bool exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec);
}

// Latin (with accents for author names), Greek and the punctuation, arrows
// and maths operators that show up in physics titles.
const ImWchar* glyph_ranges() {
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
        static const ImWchar extra[] = {
            0x0100,
            0x024F, // Latin Extended-A/B
            0x0300,
            0x036F, // Combining diacritics (t̄, ŝ)
            0x0370,
            0x03FF, // Greek
            0x2000,
            0x206F, // General punctuation (– — … ‘ ’ “ ”)
            0x2070,
            0x209F, // Super/subscripts
            0x2190,
            0x21FF, // Arrows
            0x2200,
            0x22FF, // Mathematical operators
            0,
        };
        builder.AddRanges(extra);
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

} // namespace

Fonts LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    std::string regular, bold;
    if (const char* env = std::getenv("ARXIV_GUI_FONT"); env && exists(env)) {
        regular = env;
        const char* env_bold = std::getenv("ARXIV_GUI_FONT_BOLD");
        bold = env_bold && exists(env_bold) ? env_bold : regular;
    } else {
        for (const auto& pair : kCandidates) {
            if (exists(pair.regular)) {
                regular = pair.regular;
                bold = exists(pair.bold) ? pair.bold : pair.regular;
                break;
            }
        }
    }

    Fonts fonts;
    if (regular.empty()) {
        spdlog::warn("[GUI]: No system sans font found; using ImGui's built-in font");
        fonts.body = fonts.bold = fonts.title = fonts.small = io.Fonts->AddFontDefault();
        return fonts;
    }
    spdlog::info("[GUI]: Font {} (bold {})", regular, bold);
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    const ImWchar* ranges = glyph_ranges();
    fonts.body = io.Fonts->AddFontFromFileTTF(regular.c_str(), 16.0f, &config, ranges);
    fonts.bold = io.Fonts->AddFontFromFileTTF(bold.c_str(), 16.0f, &config, ranges);
    fonts.title = io.Fonts->AddFontFromFileTTF(bold.c_str(), 22.0f, &config, ranges);
    fonts.small = io.Fonts->AddFontFromFileTTF(regular.c_str(), 13.0f, &config, ranges);
    if (!fonts.body || !fonts.bold || !fonts.title || !fonts.small) {
        spdlog::warn("[GUI]: Could not load {}; using ImGui's built-in font", regular);
        io.Fonts->Clear();
        fonts.body = fonts.bold = fonts.title = fonts.small = io.Fonts->AddFontDefault();
    }
    io.FontDefault = fonts.body;
    return fonts;
}

void ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowPadding = {16, 16};
    style.FramePadding = {12, 7};
    style.ItemSpacing = {10, 8};
    style.ItemInnerSpacing = {8, 6};
    style.ScrollbarSize = 10;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;
    style.WindowRounding = 0;
    style.ChildRounding = 12;
    style.FrameRounding = 8;
    style.PopupRounding = 10;
    style.ScrollbarRounding = 8;
    style.GrabRounding = 8;
    style.TabRounding = 8;
    style.SelectableTextAlign = {0, 0.5f};

    auto c = [](ImU32 col) { return ImGui::ColorConvertU32ToFloat4(col); };
    using namespace Palette;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = c(kText);
    colors[ImGuiCol_TextDisabled] = c(kMuted);
    colors[ImGuiCol_WindowBg] = c(kBackground);
    colors[ImGuiCol_ChildBg] = c(kSurface);
    colors[ImGuiCol_PopupBg] = c(kCard);
    colors[ImGuiCol_Border] = c(kBorder);
    colors[ImGuiCol_FrameBg] = c(kCard);
    colors[ImGuiCol_FrameBgHovered] = c(kCardHover);
    colors[ImGuiCol_FrameBgActive] = c(kCardHover);
    colors[ImGuiCol_MenuBarBg] = c(kSurface);
    colors[ImGuiCol_ScrollbarBg] = c(rgb(0, 0, 0, 0));
    colors[ImGuiCol_ScrollbarGrab] = c(kBorder);
    colors[ImGuiCol_ScrollbarGrabHovered] = c(kMuted);
    colors[ImGuiCol_ScrollbarGrabActive] = c(kAccent);
    colors[ImGuiCol_CheckMark] = c(kAccent);
    colors[ImGuiCol_Button] = c(kCard);
    colors[ImGuiCol_ButtonHovered] = c(kCardHover);
    colors[ImGuiCol_ButtonActive] = c(kAccentSoft);
    colors[ImGuiCol_Header] = c(kAccentSoft);
    colors[ImGuiCol_HeaderHovered] = c(kCardHover);
    colors[ImGuiCol_HeaderActive] = c(kAccentSoft);
    colors[ImGuiCol_Separator] = c(kBorder);
    colors[ImGuiCol_NavHighlight] = c(kAccent);
    colors[ImGuiCol_TextSelectedBg] = c(kAccentSoft);
    colors[ImGuiCol_ModalWindowDimBg] = c(rgb(0, 0, 0, 140));
}

} // namespace ArxivGui
