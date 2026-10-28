// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>
#include <vector>

namespace Arxiv::Process {

/// Run argv[0] (looked up on PATH) with the given arguments — no shell is
/// involved, so arguments are passed literally. stdin/stdout/stderr are
/// redirected to /dev/null so the program cannot draw over the TUI.
/// Returns the exit status, or -1 if the program could not be started or was
/// killed by a signal.
int RunAndWait(const std::vector<std::string>& argv);

/// Like RunAndWait, but `input` is written to the program's stdin (then
/// closed). A program that exits without reading it all is not an error for
/// the caller: no SIGPIPE reaches this process.
int RunWithInput(const std::vector<std::string>& argv, const std::string& input);

/// Like RunAndWait but returns as soon as the program has started; the child
/// is reaped in the background. Returns false if it could not be started.
bool SpawnDetached(const std::vector<std::string>& argv);

} // namespace Arxiv::Process
