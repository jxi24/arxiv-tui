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
// Installs POSIX signal handlers that, on a fatal signal, restore the
// terminal, write a crash report to disk and then re-raise the signal with
// the default action so the OS can record a core dump. The handler only uses
// async-signal-safe calls; everything it needs is prepared at install time.
//
// The crash report contains:
//   - A one-line JSON header: {"arxiv_tui_crash_report":1,"version",
//     "signal","signum","time" (UTC ISO 8601),"pid","replay_log"}, read by
//     ReplayLog::Parse
//   - Timestamp
//   - Signal name and number
//   - Backtrace (via execinfo.h backtrace_symbols_fd)
//   - The end of the replay file (if the ReplayRecorder streams to one)
// ---------------------------------------------------------------------------

/// Install signal handlers for SIGSEGV, SIGABRT, SIGFPE, SIGILL and SIGBUS
/// (SIGTERM keeps its default: it is a request to stop, not a crash). The
/// terminal mode of stdin at this point is what a crash restores. `recorder`
/// may be nullptr (no replay log in the report). `crash_dir` is the
/// directory where crash reports are written.
void InstallCrashHandler(ReplayRecorder* recorder, const std::string& crash_dir = ".");

/// Write a crash report for signal `signum` to `crash_dir`.  Returns the
/// path of the written file, or an empty string if writing failed.
/// Not for use inside a signal handler: an in-memory recorder's entries are
/// read under its lock.
std::string WriteCrashReport(int signum, ReplayRecorder* recorder, const std::string& crash_dir);

} // namespace Arxiv
