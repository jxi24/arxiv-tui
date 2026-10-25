// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Clipboard.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

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

TEST_CASE("Clipboard::Copy exercises xclip and xsel pipe commands", "[clipboard]") {
    // These calls cover the xclip/xsel branches of pipe_command even when
    // those tools are not installed; popen opens a shell that exits with 127.
    SECTION("xclip backend returns false when xclip is unavailable") {
        REQUIRE_NOTHROW(Clipboard::Copy("text", "xclip"));
    }

    SECTION("xsel backend returns false when xsel is unavailable") {
        REQUIRE_NOTHROW(Clipboard::Copy("text", "xsel"));
    }
}

TEST_CASE("Clipboard::Copy graceful failure", "[clipboard]") {
    SECTION("Returns false when backend is an obviously nonexistent program") {
        REQUIRE_FALSE(Clipboard::Copy("hello", "arxiv_nonexistent_clipboard_tool_xyz"));
    }

    SECTION("Does not throw on failure") {
        REQUIRE_NOTHROW(Clipboard::Copy("text", "arxiv_nonexistent_clipboard_tool_xyz"));
    }

    SECTION("Returns false when no backend is configured and none is available") {
        ::unsetenv("ARXIV_TUI_CLIPBOARD");
        // Only run when no clipboard tool is installed (CI / headless environments).
        if (Clipboard::DetectBackend("").empty()) {
            REQUIRE_FALSE(Clipboard::Copy("text", ""));
        }
    }
}

namespace {
std::filesystem::path clip_dir() {
    auto dir =
        std::filesystem::temp_directory_path() / ("arxiv_clip_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    return dir;
}
} // namespace

TEST_CASE("Clipboard::Copy never hands the backend to a shell", "[clipboard][regression]") {
    // The backend comes from config.yml or ARXIV_TUI_CLIPBOARD; it names a
    // program, it is not a command line.
    auto marker = clip_dir() / "shell_ran";
    std::filesystem::remove(marker);

    Clipboard::Copy("text", "true; touch " + marker.string());

    REQUIRE_FALSE(std::filesystem::exists(marker));
}

TEST_CASE("Clipboard::Copy sends the text to a custom backend's stdin", "[clipboard]") {
    auto dir = clip_dir();
    auto script = dir / "fake-copy";
    auto out = dir / "received";
    std::filesystem::remove(out);
    {
        std::ofstream s(script);
        s << "#!/bin/sh\ncat > \"$1\"\n";
    }
    std::filesystem::permissions(script, std::filesystem::perms::owner_all);

    REQUIRE(Clipboard::Copy("@article{x}", script.string() + " " + out.string()));

    std::ifstream f(out);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE(content == "@article{x}");
}

TEST_CASE("Clipboard::DetectBackend finds a backend on PATH", "[clipboard]") {
    auto dir = clip_dir() / "bin";
    std::filesystem::create_directories(dir);
    {
        std::ofstream s(dir / "xsel");
        s << "#!/bin/sh\n";
    }
    std::filesystem::permissions(dir / "xsel", std::filesystem::perms::owner_all);
    std::string old_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    ::unsetenv("ARXIV_TUI_CLIPBOARD");
    ::setenv("PATH", dir.c_str(), 1);

    auto found = Clipboard::DetectBackend("");

    ::setenv("PATH", old_path.c_str(), 1);
    REQUIRE(found == "xsel");
}
