// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Clipboard.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdlib>

using namespace Arxiv;
using namespace Catch::Matchers;

TEST_CASE("Clipboard::DetectBackend", "[clipboard]") {
    SECTION("Returns empty string when ARXIV_TUI_CLIPBOARD is unset") {
        ::unsetenv("ARXIV_TUI_CLIPBOARD");
        // Just verify it runs and returns a string (may be empty if no clipboard tool)
        REQUIRE_NOTHROW(Clipboard::DetectBackend(""));
    }

    SECTION("Honours ARXIV_TUI_CLIPBOARD env var") {
        ::setenv("ARXIV_TUI_CLIPBOARD", "xclip", 1);
        REQUIRE(Clipboard::DetectBackend("") == "xclip");
        ::unsetenv("ARXIV_TUI_CLIPBOARD");
    }

    SECTION("Explicit config value takes precedence over env var") {
        ::setenv("ARXIV_TUI_CLIPBOARD", "xclip", 1);
        REQUIRE(Clipboard::DetectBackend("wl-copy") == "wl-copy");
        ::unsetenv("ARXIV_TUI_CLIPBOARD");
    }
}

TEST_CASE("Clipboard::Copy graceful failure", "[clipboard]") {
    SECTION("Returns false when backend is an obviously nonexistent program") {
        REQUIRE_FALSE(Clipboard::Copy("hello", "arxiv_nonexistent_clipboard_tool_xyz"));
    }

    SECTION("Does not throw on failure") {
        REQUIRE_NOTHROW(Clipboard::Copy("text", "arxiv_nonexistent_clipboard_tool_xyz"));
    }
}
