// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// The GUI's look: palette, ImGui style and fonts.

#pragma once

#include "imgui.h"

namespace ArxivGui {

/// One dark palette with a single accent, used everywhere instead of ad-hoc
/// colours.
namespace Palette {
inline constexpr ImU32 rgb(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
    return IM_COL32(r, g, b, a);
}
inline constexpr ImU32 kBackground = rgb(13, 15, 20);
inline constexpr ImU32 kSurface = rgb(20, 23, 30); // sidebar, header, panes
inline constexpr ImU32 kCard = rgb(27, 31, 40);    // article cards
inline constexpr ImU32 kCardHover = rgb(34, 39, 50);
inline constexpr ImU32 kBorder = rgb(41, 47, 59);
inline constexpr ImU32 kText = rgb(230, 233, 240);
inline constexpr ImU32 kMuted = rgb(139, 147, 167);
inline constexpr ImU32 kAccent = rgb(122, 162, 247); // links, selection, primary
inline constexpr ImU32 kAccentSoft = rgb(122, 162, 247, 40);
inline constexpr ImU32 kStar = rgb(245, 196, 81);
inline constexpr ImU32 kDanger = rgb(240, 113, 120);
inline constexpr ImU32 kSuccess = rgb(115, 201, 145);
} // namespace Palette

/// Nerd Font icons (Font Awesome code points, the same in Nerd Fonts v2 and
/// v3). Only drawn when Fonts::icons is set.
namespace Icon {
inline constexpr const char kSearch[] = u8"\uf002";
inline constexpr const char kCheck[] = u8"\uf00c";
inline constexpr const char kStar[] = u8"\uf005";
inline constexpr const char kDownload[] = u8"\uf019";
inline constexpr const char kInbox[] = u8"\uf01c";
inline constexpr const char kRefresh[] = u8"\uf021";
inline constexpr const char kTag[] = u8"\uf02b";
inline constexpr const char kBookmark[] = u8"\uf02e";
inline constexpr const char kPencil[] = u8"\uf040";
inline constexpr const char kWarning[] = u8"\uf071";
inline constexpr const char kCalendar[] = u8"\uf073";
inline constexpr const char kFolder[] = u8"\uf07b";
inline constexpr const char kExternal[] = u8"\uf08e";
inline constexpr const char kBookmarkOutline[] = u8"\uf097";
inline constexpr const char kUsers[] = u8"\uf0c0";
inline constexpr const char kLink[] = u8"\uf0c1";
inline constexpr const char kLightbulb[] = u8"\uf0eb";
inline constexpr const char kEnvelope[] = u8"\uf0e0";
inline constexpr const char kBolt[] = u8"\uf0e7";
inline constexpr const char kCalendarRange[] = u8"\uf133";
inline constexpr const char kEllipsis[] = u8"\uf141";
} // namespace Icon

struct Fonts {
    ImFont* body = nullptr;  // 16 px regular
    ImFont* bold = nullptr;  // 16 px semibold
    ImFont* title = nullptr; // 22 px semibold
    ImFont* small = nullptr; // 13 px regular
    bool icons = false;      // a Nerd Font's icons are merged into each font
};

/// Load a modern sans-serif from the system (Inter, Noto Sans, Cantarell,
/// DejaVu Sans; ARXIV_GUI_FONT / ARXIV_GUI_FONT_BOLD override) and fall back
/// to ImGui's built-in font when none is found. When a Nerd Font is
/// installed (FontSearch.hh; ARXIV_GUI_ICON_FONT overrides, empty turns it
/// off) the Icon glyphs are merged into every font.
Fonts LoadFonts();

/// Rounded, roomy style in the palette above.
void ApplyStyle();

} // namespace ArxivGui
