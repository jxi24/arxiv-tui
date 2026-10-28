// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Paths.hh"

#include "Arxiv/InstallPaths.hh"

#include <cstdlib>
#include <unistd.h>

namespace Arxiv {

namespace {

// Return env[var] if set and non-empty, else $HOME/fallback, else /tmp/fallback.
std::filesystem::path xdg_or_home(const std::function<const char*(const char*)>& getenv,
                                  const char* var,
                                  std::string_view fallback) {
    const char* val = getenv(var);
    if (val && *val)
        return std::filesystem::path(val);
    const char* home = getenv("HOME");
    return std::filesystem::path(home ? home : "/tmp") / fallback;
}

// XDG variable if set; else the compiled-in install dir if this user can
// write there; else the XDG default under $HOME. A default build bakes in
// /usr/local/..., which only root can write, so without the usability check
// every ordinary user failed at startup creating the directory.
std::filesystem::path resolve_dir(const std::function<const char*(const char*)>& getenv,
                                  const char* var,
                                  std::string_view install_dir,
                                  std::string_view home_fallback,
                                  const std::function<bool(const std::filesystem::path&)>& usable) {
    const char* val = getenv(var);
    if (val && *val)
        return std::filesystem::path(val) / "arxiv-tui";
    if (!install_dir.empty()) {
        auto candidate = std::filesystem::path(install_dir) / "arxiv-tui";
        if (usable(candidate))
            return candidate;
    }
    return xdg_or_home(getenv, var, home_fallback) / "arxiv-tui";
}

bool is_usable_dir(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec && ::access(dir.c_str(), W_OK) == 0;
}

} // namespace

Paths Paths::Resolve() {
    return Resolve([](const char* name) { return std::getenv(name); },
                   InstallPaths::kDataDir,
                   InstallPaths::kStateDir,
                   is_usable_dir);
}

Paths Paths::Resolve(const std::function<const char*(const char*)>& getenv,
                     std::string_view install_data_dir,
                     std::string_view install_state_dir,
                     const std::function<bool(const std::filesystem::path&)>& usable) {
    Paths p;
    // Config is always per-user: XDG_CONFIG_HOME → ~/.config.
    p.config_file = xdg_or_home(getenv, "XDG_CONFIG_HOME", ".config") / "arxiv-tui" / "config.yml";
    p.data_dir = resolve_dir(getenv, "XDG_DATA_HOME", install_data_dir, ".local/share", usable);
    p.state_dir = resolve_dir(getenv, "XDG_STATE_HOME", install_state_dir, ".local/state", usable);
    return p;
}

} // namespace Arxiv
