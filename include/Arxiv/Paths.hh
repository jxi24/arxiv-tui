// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>
#include <functional>
#include <string_view>

namespace Arxiv {

// Resolved filesystem paths following the XDG Base Directory Specification.
// All paths are absolute and platform-specific; callers should create the
// directories they need before opening files inside them.
struct Paths {
    std::filesystem::path config_file; // …/arxiv-tui/config.yml
    std::filesystem::path data_dir;    // articles.db, ranker.bin, downloads/
    std::filesystem::path state_dir;   // rotating log, replay.jsonl, crash reports

    // Resolve paths from the environment. Honours XDG_CONFIG_HOME,
    // XDG_DATA_HOME, and XDG_STATE_HOME; falls back to the XDG defaults
    // (~/.config, ~/.local/share, ~/.local/state) when they are unset.
    static Paths Resolve();

    // Testable core of Resolve(): `getenv` looks up environment variables,
    // the install dirs are the compiled-in defaults, and `usable` reports
    // whether a directory exists (or can be created) and is writable.
    static Paths Resolve(const std::function<const char*(const char*)>& getenv,
                         std::string_view install_data_dir,
                         std::string_view install_state_dir,
                         const std::function<bool(const std::filesystem::path&)>& usable);
};

} // namespace Arxiv
