// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "FontSearch.hh"

#include <algorithm>
#include <string>
#include <system_error>

namespace ArxivGui {

namespace fs = std::filesystem;

namespace {

bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

bool is_nerd_font(const fs::path& p) {
    const std::string ext = p.extension().string();
    if (ext != ".ttf" && ext != ".otf" && ext != ".TTF" && ext != ".OTF")
        return false;
    const std::string name = p.filename().string();
    return contains(name, "NerdFont") || contains(name, "Nerd Font");
}

// Lower is better: the icon-only Symbols font, then proportional, then any
// other; within each, the regular style.
int rank(const fs::path& p) {
    const std::string name = p.stem().string();
    int r = contains(name, "Symbols") ? 0 : contains(name, "Propo") ? 1 : 2;
    for (const char* style : {"Bold", "Italic", "Light", "Thin", "Medium", "Black", "Oblique"})
        if (contains(name, style))
            return r + 10;
    return r;
}

} // namespace

std::vector<fs::path> FontDirs(const GetEnv& getenv) {
    std::vector<fs::path> dirs;
    const char* home = getenv("HOME");
    const char* data_home = getenv("XDG_DATA_HOME");
    if (data_home && *data_home)
        dirs.push_back(fs::path(data_home) / "fonts");
    else if (home && *home)
        dirs.push_back(fs::path(home) / ".local/share/fonts");
    if (home && *home)
        dirs.push_back(fs::path(home) / ".fonts");

    const char* data_dirs = getenv("XDG_DATA_DIRS");
    std::string list = data_dirs && *data_dirs ? data_dirs : "/usr/local/share:/usr/share";
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t end = std::min(list.find(':', start), list.size());
        if (end > start)
            dirs.push_back(fs::path(list.substr(start, end - start)) / "fonts");
        start = end + 1;
    }
    return dirs;
}

std::optional<fs::path> FindNerdFont(const std::vector<fs::path>& dirs) {
    for (const auto& dir : dirs) {
        std::error_code ec;
        std::vector<fs::path> found;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it.depth() > 4) {
                it.disable_recursion_pending();
                continue;
            }
            std::error_code type_ec;
            if (it->is_regular_file(type_ec) && is_nerd_font(it->path()))
                found.push_back(it->path());
        }
        if (!found.empty())
            return *std::min_element(found.begin(), found.end(), [](const auto& a, const auto& b) {
                const int ra = rank(a), rb = rank(b);
                return ra != rb ? ra < rb : a < b;
            });
    }
    return std::nullopt;
}

std::optional<fs::path> NerdFontFor(const GetEnv& getenv) {
    if (const char* chosen = getenv("ARXIV_GUI_ICON_FONT")) {
        std::error_code ec;
        if (*chosen == '\0')
            return std::nullopt; // icons off
        if (fs::is_regular_file(chosen, ec))
            return fs::path(chosen);
    }
    return FindNerdFont(FontDirs(getenv));
}

} // namespace ArxivGui
