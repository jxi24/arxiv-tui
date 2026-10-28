// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>

namespace Arxiv::Cli {

/// The parsed command line. `error` is non-empty when it is invalid.
struct Options {
    enum class Mode { Tui, Help, Version, Fetch, Export, Replay };
    Mode mode = Mode::Tui;
    bool trace = false;
    std::string replay_file;
    std::string export_today;
    std::string export_yaml;
    std::string error;
};

Options Parse(int argc, const char* const* argv);

/// --help text.
std::string Usage();

/// --version text: "arxiv-tui <version>".
std::string VersionText();

} // namespace Arxiv::Cli
