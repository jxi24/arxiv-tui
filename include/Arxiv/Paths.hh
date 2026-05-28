// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>

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
};

} // namespace Arxiv
