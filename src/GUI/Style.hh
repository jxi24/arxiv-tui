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

struct Fonts {
    ImFont* body = nullptr;  // 16 px regular
    ImFont* bold = nullptr;  // 16 px semibold
    ImFont* title = nullptr; // 22 px semibold
    ImFont* small = nullptr; // 13 px regular
};

/// Load a modern sans-serif from the system (Inter, Noto Sans, Cantarell,
/// DejaVu Sans; ARXIV_GUI_FONT / ARXIV_GUI_FONT_BOLD override) and fall back
/// to ImGui's built-in font when none is found.
Fonts LoadFonts();

/// Rounded, roomy style in the palette above.
void ApplyStyle();

} // namespace ArxivGui
