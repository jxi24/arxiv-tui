// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Finding an installed Nerd Font for the GUI's icons. Plain filesystem code
// (no ImGui), so the unit tests build it too.

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace ArxivGui {

using GetEnv = std::function<const char*(const char*)>;

/// Where fonts are installed, user directories first: $XDG_DATA_HOME/fonts
/// (~/.local/share/fonts), ~/.fonts, then <dir>/fonts for each entry of
/// $XDG_DATA_DIRS (/usr/local/share:/usr/share).
std::vector<std::filesystem::path> FontDirs(const GetEnv& getenv);

/// The best Nerd Font (.ttf/.otf whose name says "NerdFont" or "Nerd Font")
/// under `dirs`, searched in order: the icon-only Symbols font first, then
/// any other, regular style before bold or italic.
std::optional<std::filesystem::path> FindNerdFont(const std::vector<std::filesystem::path>& dirs);

/// $ARXIV_GUI_ICON_FONT when it names a file (empty turns icons off),
/// otherwise FindNerdFont(FontDirs(getenv)).
std::optional<std::filesystem::path> NerdFontFor(const GetEnv& getenv);

} // namespace ArxivGui
