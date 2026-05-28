// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>

namespace Arxiv {

class ReplayRecorder;

// ---------------------------------------------------------------------------
// CrashHandler
//
// Installs POSIX signal handlers that, on a fatal signal, write a crash
// report to disk and then re-raise the signal with the default handler so
// the OS can record a core dump and the process exits cleanly.
//
// The crash report contains:
//   - Timestamp
//   - Signal name and number
//   - Backtrace (via execinfo.h backtrace_symbols)
//   - Replay log (if a ReplayRecorder was provided)
// ---------------------------------------------------------------------------

/// Install signal handlers for SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS,
/// SIGTERM.  `recorder` may be nullptr (no replay log will be written).
/// `crash_dir` is the directory where crash reports are written; defaults to
/// the current working directory.
void InstallCrashHandler(ReplayRecorder* recorder, const std::string& crash_dir = ".");

/// Write a crash report for signal `signum` to `crash_dir`.  Returns the
/// path of the written file, or an empty string if writing failed.
/// This function is intentionally kept as a free function so it can be called
/// from tests without installing signal handlers.
std::string WriteCrashReport(int signum, ReplayRecorder* recorder, const std::string& crash_dir);

} // namespace Arxiv
