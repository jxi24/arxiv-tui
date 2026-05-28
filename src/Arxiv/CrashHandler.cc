// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/CrashHandler.hh"
#include "Arxiv/Replay.hh"

#include <csignal>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#ifdef __linux__
#include <execinfo.h>
#endif

namespace Arxiv {

namespace {

// Global state for the signal handler (signal handlers must use only
// async-signal-safe operations; the recorder pointer is read-only here).
static ReplayRecorder* g_recorder  = nullptr;
static char            g_crash_dir[4096] = ".";

const char* SignalName(int signum) {
    switch (signum) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        case SIGBUS:  return "SIGBUS";
        case SIGTERM: return "SIGTERM";
        default:      return "UNKNOWN";
    }
}

void SignalHandler(int signum) {
    // WriteCrashReport uses C++ I/O — not strictly async-signal-safe but
    // acceptable for a crash reporter whose purpose is best-effort.
    WriteCrashReport(signum, g_recorder, std::string(g_crash_dir));

    // Re-raise with default handler so the OS can record a core dump.
    signal(signum, SIG_DFL);
    raise(signum);
}

} // anonymous namespace

// ---------------------------------------------------------------------------

void InstallCrashHandler(ReplayRecorder* recorder, const std::string& crash_dir) {
    g_recorder = recorder;
    // Copy crash_dir into static buffer (signal handlers cannot use std::string)
    std::size_t n = crash_dir.size();
    if (n >= sizeof(g_crash_dir)) n = sizeof(g_crash_dir) - 1;
    crash_dir.copy(g_crash_dir, n);
    g_crash_dir[n] = '\0';

    signal(SIGSEGV, SignalHandler);
    signal(SIGABRT, SignalHandler);
    signal(SIGFPE,  SignalHandler);
    signal(SIGILL,  SignalHandler);
    signal(SIGBUS,  SignalHandler);
    signal(SIGTERM, SignalHandler);
}

std::string WriteCrashReport(int signum, ReplayRecorder* recorder, const std::string& crash_dir) {
    // Build timestamp string
    std::time_t now = std::time(nullptr);
    char ts_buf[32];
    std::strftime(ts_buf, sizeof(ts_buf), "%Y%m%d_%H%M%S", std::gmtime(&now));

    // Build file path
    std::string path = crash_dir + "/crash_" + ts_buf + "_" + SignalName(signum) + ".txt";

    std::ofstream f(path);
    if (!f.is_open()) {
        // Try current directory as fallback
        path = std::string("./crash_") + ts_buf + "_" + SignalName(signum) + ".txt";
        f.open(path);
        if (!f.is_open()) return "";
    }

    f << "=== arxiv-tui Crash Report ===\n";
    f << "Timestamp : " << ts_buf << "\n";
    f << "Signal    : " << SignalName(signum) << " (" << signum << ")\n\n";

    // Backtrace
    f << "--- Backtrace ---\n";
#ifdef __linux__
    void* bt_buf[64];
    int   bt_size = backtrace(bt_buf, 64);
    char** bt_syms = backtrace_symbols(bt_buf, bt_size);
    if (bt_syms) {
        for (int i = 0; i < bt_size; ++i) {
            f << "  " << bt_syms[i] << "\n";
        }
        free(bt_syms);
    } else {
        f << "  (backtrace unavailable)\n";
    }
#else
    f << "  (backtrace not supported on this platform)\n";
#endif

    // Replay log
    if (recorder && recorder->GetCount() > 0) {
        f << "\n--- Replay Log (" << recorder->GetCount() << " entries) ---\n";
        f << recorder->GetJSONL();
    } else {
        f << "\n--- Replay Log (empty) ---\n";
    }

    f << "\n=== End of Report ===\n";
    f.close();
    return path;
}

} // namespace Arxiv
