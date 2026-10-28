// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Finding an installed Nerd Font for the GUI's icons (src/GUI/FontSearch).

#include "FontSearch.hh"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        std::string tmpl = (fs::temp_directory_path() / "arxiv-fonts-XXXXXX").string();
        path = ::mkdtemp(tmpl.data());
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    fs::path Add(const std::string& relative) const {
        const fs::path p = path / relative;
        fs::create_directories(p.parent_path());
        std::ofstream(p) << "font";
        return p;
    }
};

ArxivGui::GetEnv env(std::map<std::string, std::string> vars) {
    return [vars = std::move(vars)](const char* name) -> const char* {
        auto it = vars.find(name);
        return it == vars.end() ? nullptr : it->second.c_str();
    };
}

} // namespace

TEST_CASE("FindNerdFont finds nothing without a Nerd Font", "[gui][fonts]") {
    TempDir dir;
    dir.Add("truetype/dejavu/DejaVuSans.ttf");
    REQUIRE_FALSE(ArxivGui::FindNerdFont({dir.path}).has_value());
}

TEST_CASE("FindNerdFont finds a Nerd Font in a sub-directory", "[gui][fonts]") {
    TempDir dir;
    const auto font = dir.Add("nerd/JetBrainsMono/JetBrainsMonoNerdFont-Regular.ttf");
    REQUIRE(ArxivGui::FindNerdFont({dir.path}) == font);
}

TEST_CASE("FindNerdFont accepts the v2 \"Nerd Font Complete\" names", "[gui][fonts]") {
    TempDir dir;
    const auto font = dir.Add("Hack Regular Nerd Font Complete.ttf");
    REQUIRE(ArxivGui::FindNerdFont({dir.path}) == font);
}

TEST_CASE("FindNerdFont ignores files that are not fonts", "[gui][fonts]") {
    TempDir dir;
    dir.Add("SymbolsNerdFont-Regular.txt");
    REQUIRE_FALSE(ArxivGui::FindNerdFont({dir.path}).has_value());
}

TEST_CASE("FindNerdFont prefers the icon-only Symbols font", "[gui][fonts]") {
    TempDir dir;
    dir.Add("a/FiraCodeNerdFont-Regular.ttf");
    const auto symbols = dir.Add("z/SymbolsNerdFont-Regular.ttf");
    REQUIRE(ArxivGui::FindNerdFont({dir.path}) == symbols);
}

TEST_CASE("FindNerdFont prefers the regular style", "[gui][fonts]") {
    TempDir dir;
    dir.Add("HackNerdFont-Bold.ttf");
    dir.Add("HackNerdFont-Italic.ttf");
    const auto regular = dir.Add("HackNerdFont-Regular.ttf");
    REQUIRE(ArxivGui::FindNerdFont({dir.path}) == regular);
}

TEST_CASE("FindNerdFont searches the directories in order", "[gui][fonts]") {
    TempDir user, system;
    const auto mine = user.Add("UbuntuNerdFont-Regular.ttf");
    system.Add("UbuntuNerdFont-Regular.ttf");
    REQUIRE(ArxivGui::FindNerdFont({user.path, system.path}) == mine);
}

TEST_CASE("FindNerdFont skips directories that do not exist", "[gui][fonts]") {
    TempDir dir;
    const auto font = dir.Add("SymbolsNerdFont-Regular.ttf");
    REQUIRE(ArxivGui::FindNerdFont({dir.path / "missing", dir.path}) == font);
}

TEST_CASE("FontDirs follows the XDG layout", "[gui][fonts]") {
    SECTION("defaults under HOME, then /usr/local/share and /usr/share") {
        const auto dirs = ArxivGui::FontDirs(env({{"HOME", "/home/u"}}));
        REQUIRE(dirs == std::vector<fs::path>{"/home/u/.local/share/fonts",
                                              "/home/u/.fonts",
                                              "/usr/local/share/fonts",
                                              "/usr/share/fonts"});
    }
    SECTION("XDG_DATA_HOME and XDG_DATA_DIRS override them") {
        const auto dirs = ArxivGui::FontDirs(
            env({{"HOME", "/home/u"}, {"XDG_DATA_HOME", "/d"}, {"XDG_DATA_DIRS", "/a:/b"}}));
        REQUIRE(dirs ==
                std::vector<fs::path>{"/d/fonts", "/home/u/.fonts", "/a/fonts", "/b/fonts"});
    }
}

TEST_CASE("NerdFontFor honours ARXIV_GUI_ICON_FONT", "[gui][fonts]") {
    TempDir dir;
    const auto chosen = dir.Add("custom/icons.ttf");
    SECTION("an existing file wins") {
        REQUIRE(ArxivGui::NerdFontFor(env({{"ARXIV_GUI_ICON_FONT", chosen.string()},
                                           {"XDG_DATA_DIRS", dir.path.string()}})) == chosen);
    }
    SECTION("an empty value turns icons off") {
        dir.Add("fonts/SymbolsNerdFont-Regular.ttf");
        REQUIRE_FALSE(ArxivGui::NerdFontFor(env({{"ARXIV_GUI_ICON_FONT", ""},
                                                 {"HOME", "/nonexistent"},
                                                 {"XDG_DATA_DIRS", dir.path.string()}}))
                          .has_value());
    }
    SECTION("unset, the font directories are searched") {
        const auto found = dir.Add("fonts/SymbolsNerdFont-Regular.ttf");
        REQUIRE(ArxivGui::NerdFontFor(env(
                    {{"HOME", "/nonexistent"}, {"XDG_DATA_DIRS", dir.path.string()}})) == found);
    }
}
