// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Views/Colors.hh"

#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using Arxiv::SetTheme;
using Arxiv::ThemeNames;

namespace {
// Restore the default theme whatever the test did.
struct ThemeGuard {
    ~ThemeGuard() { SetTheme("frappe"); }
};
} // namespace

TEST_CASE("Theme: presets are available by name", "[theme]") {
    const auto names = ThemeNames();
    for (const char* n : {"frappe", "latte", "mocha", "high-contrast", "terminal"})
        REQUIRE(std::find(names.begin(), names.end(), n) != names.end());
}

TEST_CASE("Theme: switching presets changes the colours", "[theme]") {
    ThemeGuard guard;
    REQUIRE(SetTheme("frappe"));
    const ftxui::Color dark_base = TextColors::base();
    REQUIRE(SetTheme("latte"));
    REQUIRE_FALSE(TextColors::base() == dark_base);
}

TEST_CASE("Theme: an unknown name is rejected and keeps the current theme", "[theme]") {
    ThemeGuard guard;
    SetTheme("mocha");
    const ftxui::Color before = TextColors::primary();
    REQUIRE_FALSE(SetTheme("no-such-theme"));
    REQUIRE(TextColors::primary() == before);
}

TEST_CASE("Theme: individual colours can be overridden with #rrggbb", "[theme]") {
    ThemeGuard guard;
    REQUIRE(SetTheme("frappe", {{"primary", "#ff0000"}, {"border", "not-a-colour"}}));

    SECTION("valid override applies") {
        REQUIRE(TextColors::primary() == ftxui::Color::RGB(255, 0, 0));
    }
    SECTION("invalid override is ignored") {
        SetTheme("frappe");
        const ftxui::Color frappe_border = TextColors::border();
        SetTheme("frappe", {{"border", "not-a-colour"}});
        REQUIRE(TextColors::border() == frappe_border);
    }
}
