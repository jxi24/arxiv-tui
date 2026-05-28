// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Paths.hh"

#include "Arxiv/InstallPaths.hh"

#include <cstdlib>

namespace Arxiv {

namespace {

// Return env[var] if set and non-empty, else $HOME/fallback, else /tmp/fallback.
std::filesystem::path xdg_or_home(const char* var, std::string_view fallback) {
    const char* val = std::getenv(var);
    if (val && *val)
        return std::filesystem::path(val);
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : "/tmp") / fallback;
}

} // namespace

Paths Paths::Resolve() {
    Paths p;

    // Config is always per-user: XDG_CONFIG_HOME → ~/.config.
    p.config_file = xdg_or_home("XDG_CONFIG_HOME", ".config") / "arxiv-tui" / "config.yml";

    // Data: XDG_DATA_HOME takes precedence over the compile-time install dir.
    const char* data_home = std::getenv("XDG_DATA_HOME");
    if (data_home && *data_home)
        p.data_dir = std::filesystem::path(data_home) / "arxiv-tui";
    else if (!InstallPaths::kDataDir.empty())
        p.data_dir = std::filesystem::path(InstallPaths::kDataDir) / "arxiv-tui";
    else
        p.data_dir = xdg_or_home("XDG_DATA_HOME", ".local/share") / "arxiv-tui";

    // State (logs, replay, crash reports): XDG_STATE_HOME takes precedence.
    const char* state_home = std::getenv("XDG_STATE_HOME");
    if (state_home && *state_home)
        p.state_dir = std::filesystem::path(state_home) / "arxiv-tui";
    else if (!InstallPaths::kStateDir.empty())
        p.state_dir = std::filesystem::path(InstallPaths::kStateDir) / "arxiv-tui";
    else
        p.state_dir = xdg_or_home("XDG_STATE_HOME", ".local/state") / "arxiv-tui";

    return p;
}

} // namespace Arxiv
