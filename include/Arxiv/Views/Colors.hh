// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <ftxui/screen/color.hpp>

namespace TextColors {
// Catppuccin Frappe palette — initialized lazily to avoid SIOF with FTXUI statics
inline const ftxui::Color& base()      { static ftxui::Color c = ftxui::Color::RGB( 48,  52,  70); return c; }
inline const ftxui::Color& surface()   { static ftxui::Color c = ftxui::Color::RGB( 65,  69,  89); return c; }
inline const ftxui::Color& text()      { static ftxui::Color c = ftxui::Color::RGB(198, 208, 245); return c; }
inline const ftxui::Color& subtext()   { static ftxui::Color c = ftxui::Color::RGB(165, 173, 206); return c; }
inline const ftxui::Color& primary()   { static ftxui::Color c = ftxui::Color::RGB(140, 170, 238); return c; }
inline const ftxui::Color& border()    { static ftxui::Color c = ftxui::Color::RGB(153, 209, 219); return c; }
inline const ftxui::Color& secondary() { static ftxui::Color c = ftxui::Color::RGB(244, 184, 228); return c; }
inline const ftxui::Color& error()     { static ftxui::Color c = ftxui::Color::RGB(231, 130, 132); return c; }
}
