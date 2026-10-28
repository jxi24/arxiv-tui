// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <ftxui/screen/color.hpp>

#include <map>
#include <string>
#include <vector>

namespace Arxiv {
/// Switch the colour theme ("frappe", "latte", "mocha", "high-contrast",
/// "terminal"), optionally overriding individual colours by name with
/// "#rrggbb". Returns false (and keeps the current theme) for an unknown name.
bool SetTheme(const std::string& name, const std::map<std::string, std::string>& overrides = {});
std::vector<std::string> ThemeNames();
} // namespace Arxiv

namespace Arxiv {
/// The eight colours every view draws with.
struct Theme {
    ftxui::Color base, surface, text, subtext, primary, border, secondary, error;
};
/// The theme in use (set by SetTheme; "frappe" until then). UI thread only.
const Theme& CurrentTheme();
} // namespace Arxiv

namespace TextColors {
inline ftxui::Color base() { return Arxiv::CurrentTheme().base; }
inline ftxui::Color surface() { return Arxiv::CurrentTheme().surface; }
inline ftxui::Color text() { return Arxiv::CurrentTheme().text; }
inline ftxui::Color subtext() { return Arxiv::CurrentTheme().subtext; }
inline ftxui::Color primary() { return Arxiv::CurrentTheme().primary; }
inline ftxui::Color border() { return Arxiv::CurrentTheme().border; }
inline ftxui::Color secondary() { return Arxiv::CurrentTheme().secondary; }
inline ftxui::Color error() { return Arxiv::CurrentTheme().error; }
} // namespace TextColors
