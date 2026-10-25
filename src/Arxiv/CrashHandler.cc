// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/CrashHandler.hh"

#include "Arxiv/Replay.hh"

#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#ifdef __linux__
#include <execinfo.h>
#endif

namespace Arxiv {

namespace {

// Everything the signal handler needs is prepared at install time. The
// handler itself only uses async-signal-safe calls (open/write/read/lseek,
// tcsetattr, sigaction, raise): no allocation, no locks, no C++ streams.
constexpr int kFatalSignals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
constexpr std::size_t kPathMax = 4096;
// How much of the end of the replay file goes into a report.
constexpr off_t kReplayTailBytes = 64 * 1024;

char g_crash_dir[kPathMax] = ".";
char g_replay_path[kPathMax] = "";
termios g_saved_termios{};
bool g_have_termios = false;

const char* SignalName(int signum) {
    switch (signum) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGABRT:
        return "SIGABRT";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGBUS:
        return "SIGBUS";
    case SIGTERM:
        return "SIGTERM";
    default:
        return "UNKNOWN";
    }
}

void copy_bounded(char (&dst)[kPathMax], const std::string& src) {
    std::size_t n = src.size() < kPathMax - 1 ? src.size() : kPathMax - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

// --- async-signal-safe output helpers ------------------------------------

void put(int fd, const char* s, std::size_t n) {
    while (n > 0) {
        ssize_t w = ::write(fd, s, n);
        if (w <= 0)
            return;
        s += w;
        n -= static_cast<std::size_t>(w);
    }
}

void put(int fd, const char* s) { put(fd, s, std::strlen(s)); }

// Append `text` to the buffer at `pos` (always NUL-terminated).
void append(char* buf, std::size_t cap, std::size_t& pos, const char* text) {
    while (*text && pos + 1 < cap)
        buf[pos++] = *text++;
    buf[pos] = '\0';
}

void append_number(char* buf, std::size_t cap, std::size_t& pos, long long value, int width) {
    char digits[24];
    int n = 0;
    bool negative = value < 0;
    unsigned long long v = negative ? 0ULL - static_cast<unsigned long long>(value)
                                    : static_cast<unsigned long long>(value);
    do {
        digits[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v > 0 && n < 20);
    while (n < width)
        digits[n++] = '0';
    if (negative)
        digits[n++] = '-';
    while (n > 0 && pos + 1 < cap)
        buf[pos++] = digits[--n];
    buf[pos] = '\0';
}

// "YYYYmmdd_HHMMSS" in UTC without gmtime (which is not signal-safe).
void append_timestamp(char* buf, std::size_t cap, std::size_t& pos, std::time_t now) {
    long long days = static_cast<long long>(now) / 86400;
    long long secs = static_cast<long long>(now) % 86400;
    // Civil-from-days (Howard Hinnant).
    long long z = days + 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    long long d = doy - (153 * mp + 2) / 5 + 1;
    long long m = mp < 10 ? mp + 3 : mp - 9;
    long long y = yoe + era * 400 + (m <= 2 ? 1 : 0);
    append_number(buf, cap, pos, y, 4);
    append_number(buf, cap, pos, m, 2);
    append_number(buf, cap, pos, d, 2);
    append(buf, cap, pos, "_");
    append_number(buf, cap, pos, secs / 3600, 2);
    append_number(buf, cap, pos, (secs / 60) % 60, 2);
    append_number(buf, cap, pos, secs % 60, 2);
}

// Copy the last kReplayTailBytes of a regular file into `out`.
void copy_tail(int out, const char* path) {
    if (!path || !*path)
        return;
    int in = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (in < 0)
        return;
    struct stat st {};
    if (::fstat(in, &st) == 0 && S_ISREG(st.st_mode)) {
        if (st.st_size > kReplayTailBytes) {
            ::lseek(in, st.st_size - kReplayTailBytes, SEEK_SET);
            put(out, "(earlier entries omitted)\n");
        }
        char chunk[4096];
        ssize_t n;
        while ((n = ::read(in, chunk, sizeof(chunk))) > 0)
            put(out, chunk, static_cast<std::size_t>(n));
    }
    ::close(in);
}

// Write a report to `dir` and return its fd (still open) and path, or -1.
int write_report(int signum, const char* dir, const char* replay_path, char (&path)[kPathMax]) {
    char stamp[32];
    std::size_t sp = 0;
    append_timestamp(stamp, sizeof(stamp), sp, std::time(nullptr));

    auto build_path = [&](const char* base) {
        std::size_t pos = 0;
        append(path, kPathMax, pos, base);
        append(path, kPathMax, pos, "/crash_");
        append(path, kPathMax, pos, stamp);
        append(path, kPathMax, pos, "_");
        append(path, kPathMax, pos, SignalName(signum));
        append(path, kPathMax, pos, "_");
        append_number(path, kPathMax, pos, ::getpid(), 1);
        append(path, kPathMax, pos, ".txt");
    };
    // No fallback to the working directory: a report there litters wherever
    // the user happened to start the program.
    build_path(dir);
    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;

    put(fd, "=== arxiv-tui Crash Report ===\nTimestamp : ");
    put(fd, stamp);
    put(fd, "\nSignal    : ");
    put(fd, SignalName(signum));
    char num[24];
    std::size_t np = 0;
    append(num, sizeof(num), np, " (");
    append_number(num, sizeof(num), np, signum, 1);
    append(num, sizeof(num), np, ")\n\n");
    put(fd, num);

    put(fd, "--- Backtrace ---\n");
#ifdef __linux__
    void* frames[64];
    int count = ::backtrace(frames, 64);
    ::backtrace_symbols_fd(frames, count, fd); // writes directly, no malloc
#else
    put(fd, "  (backtrace not supported on this platform)\n");
#endif

    if (replay_path && *replay_path) {
        put(fd, "\n--- Replay Log (");
        put(fd, replay_path);
        put(fd, ") ---\n");
        copy_tail(fd, replay_path);
    }
    return fd;
}

void restore_terminal() {
    if (g_have_termios)
        ::tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
    if (::isatty(STDOUT_FILENO)) {
        // Leave the alternate screen, stop mouse reporting, show the cursor.
        put(STDOUT_FILENO,
            "\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1006l\x1b[?1015l"
            "\x1b[?1049l\x1b[?25h\x1b[0m\r\n");
    }
}

void SignalHandler(int signum) {
    // SA_RESETHAND already restored the default action for this signal.
    restore_terminal();

    char path[kPathMax];
    int fd = write_report(signum, g_crash_dir, g_replay_path, path);
    if (fd >= 0) {
        put(fd, "\n=== End of Report ===\n");
        ::close(fd);
        put(STDERR_FILENO, "arxiv-tui crashed; report written to ");
        put(STDERR_FILENO, path);
        put(STDERR_FILENO, "\n");
    }

    ::raise(signum); // default action: terminate (and core dump if enabled)
}

} // anonymous namespace

// ---------------------------------------------------------------------------

void InstallCrashHandler(ReplayRecorder* recorder, const std::string& crash_dir) {
    copy_bounded(g_crash_dir, crash_dir);
    copy_bounded(g_replay_path, recorder ? recorder->GetFilePath() : std::string());
    g_have_termios = ::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &g_saved_termios) == 0;

#ifdef __linux__
    // The first backtrace() call loads libgcc (which allocates); do it now
    // rather than inside the handler.
    void* frame[1];
    ::backtrace(frame, 1);
#endif

    struct sigaction sa {};
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = static_cast<int>(SA_RESETHAND);
    for (int sig : kFatalSignals)
        ::sigaction(sig, &sa, nullptr);
}

std::string WriteCrashReport(int signum, ReplayRecorder* recorder, const std::string& crash_dir) {
    char dir[kPathMax];
    copy_bounded(dir, crash_dir);
    std::string replay_path = recorder ? recorder->GetFilePath() : std::string();
    char path[kPathMax];
    int fd = write_report(signum, dir, replay_path.c_str(), path);
    if (fd < 0)
        return "";
    // Outside a signal handler the in-memory log is safe to read.
    if (recorder && replay_path.empty()) {
        put(fd, "\n--- Replay Log (in memory) ---\n");
        std::string jsonl = recorder->GetJSONL();
        put(fd, jsonl.data(), jsonl.size());
    }
    put(fd, "\n=== End of Report ===\n");
    ::close(fd);
    return path;
}

} // namespace Arxiv
