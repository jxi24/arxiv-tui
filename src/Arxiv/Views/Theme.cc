// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Views/Colors.hh"

#include <array>
#include <cstdlib>
#include <utility>

#include "spdlog/spdlog.h"

namespace Arxiv {

namespace {

using ftxui::Color;

struct Preset {
    const char* name;
    Theme (*make)();
};

// Catppuccin flavours (https://catppuccin.com), a high-contrast palette, and
// "terminal", which uses the terminal's own colours (for 16-colour terminals
// or a carefully tuned terminal scheme).
const std::array<Preset, 5>& presets() {
    static const std::array<Preset, 5> list = {{
        {"frappe",
         [] {
             return Theme{Color::RGB(48, 52, 70),
                          Color::RGB(65, 69, 89),
                          Color::RGB(198, 208, 245),
                          Color::RGB(165, 173, 206),
                          Color::RGB(140, 170, 238),
                          Color::RGB(153, 209, 219),
                          Color::RGB(244, 184, 228),
                          Color::RGB(231, 130, 132)};
         }},
        {"latte",
         [] {
             return Theme{Color::RGB(239, 241, 245),
                          Color::RGB(204, 208, 218),
                          Color::RGB(76, 79, 105),
                          Color::RGB(108, 111, 133),
                          Color::RGB(30, 102, 245),
                          Color::RGB(4, 165, 229),
                          Color::RGB(234, 118, 203),
                          Color::RGB(210, 15, 57)};
         }},
        {"mocha",
         [] {
             return Theme{Color::RGB(30, 30, 46),
                          Color::RGB(49, 50, 68),
                          Color::RGB(205, 214, 244),
                          Color::RGB(166, 173, 200),
                          Color::RGB(137, 180, 250),
                          Color::RGB(148, 226, 213),
                          Color::RGB(245, 194, 231),
                          Color::RGB(243, 139, 168)};
         }},
        {"high-contrast",
         [] {
             return Theme{Color::RGB(0, 0, 0),
                          Color::RGB(40, 40, 40),
                          Color::RGB(255, 255, 255),
                          Color::RGB(210, 210, 210),
                          Color::RGB(255, 215, 0),
                          Color::RGB(255, 255, 255),
                          Color::RGB(0, 255, 255),
                          Color::RGB(255, 85, 85)};
         }},
        {"terminal",
         [] {
             return Theme{Color::Default,
                          Color::Default,
                          Color::Default,
                          Color::GrayLight,
                          Color::Blue,
                          Color::GrayDark,
                          Color::Magenta,
                          Color::Red};
         }},
    }};
    return list;
}

Theme& current() {
    static Theme theme = presets()[0].make();
    return theme;
}

// "#rrggbb" -> Color; false if malformed.
bool parse_hex(const std::string& hex, Color& out) {
    if (hex.size() != 7 || hex[0] != '#')
        return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(hex.c_str() + 1, &end, 16);
    if (*end != '\0')
        return false;
    out = Color::RGB(static_cast<std::uint8_t>((v >> 16) & 0xFF),
                     static_cast<std::uint8_t>((v >> 8) & 0xFF),
                     static_cast<std::uint8_t>(v & 0xFF));
    return true;
}

} // namespace

const Theme& CurrentTheme() { return current(); }

std::vector<std::string> ThemeNames() {
    std::vector<std::string> names;
    for (const auto& p : presets())
        names.emplace_back(p.name);
    return names;
}

bool SetTheme(const std::string& name, const std::map<std::string, std::string>& overrides) {
    const Preset* preset = nullptr;
    for (const auto& p : presets())
        if (name == p.name)
            preset = &p;
    if (!preset) {
        spdlog::warn("[Theme]: Unknown theme '{}'", name);
        return false;
    }
    Theme theme = preset->make();
    const std::pair<const char*, Color Theme::*> slots[] = {
        {"base", &Theme::base},
        {"surface", &Theme::surface},
        {"text", &Theme::text},
        {"subtext", &Theme::subtext},
        {"primary", &Theme::primary},
        {"border", &Theme::border},
        {"secondary", &Theme::secondary},
        {"error", &Theme::error},
    };
    for (const auto& [key, value] : overrides) {
        Color parsed;
        bool applied = false;
        for (const auto& [slot, member] : slots)
            if (key == slot && parse_hex(value, parsed)) {
                theme.*member = parsed;
                applied = true;
            }
        if (!applied)
            spdlog::warn("[Theme]: Ignoring colour override {}: '{}'", key, value);
    }
    current() = theme;
    return true;
}

} // namespace Arxiv
