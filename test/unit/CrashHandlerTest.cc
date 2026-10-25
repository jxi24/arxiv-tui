// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// The installed crash handler runs inside a signal handler, so it may only use
// async-signal-safe calls. Each test crashes a forked child and inspects what
// the handler left behind.

#include "Arxiv/CrashHandler.hh"

#include "Arxiv/Replay.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

using namespace Catch::Matchers;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

fs::path fresh_dir(const std::string& stem) {
    static int counter = 0;
    auto dir = fs::temp_directory_path() /
               (stem + "_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// Run `child` in a forked process (no core dumps). Waits up to `limit`; a
// child still running then is killed and reported as hung.
struct ChildResult {
    bool hung = false;
    int status = 0;
};

template <typename F> ChildResult run_child(F&& child, std::chrono::milliseconds limit = 5000ms) {
    pid_t pid = ::fork();
    if (pid == 0) {
        rlimit no_core{0, 0};
        ::setrlimit(RLIMIT_CORE, &no_core);
        child();
        ::_exit(0);
    }
    ChildResult r;
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (::waitpid(pid, &r.status, WNOHANG) == 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &r.status, 0);
            r.hung = true;
            break;
        }
        std::this_thread::sleep_for(10ms);
    }
    return r;
}

std::string only_report(const fs::path& dir) {
    std::string content;
    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().filename().string().rfind("crash_", 0) != 0)
            continue;
        ++count;
        std::ifstream f(entry.path());
        std::stringstream ss;
        ss << f.rdbuf();
        content = ss.str();
    }
    return count == 1 ? content : std::string();
}

} // namespace

TEST_CASE("Crash handler: SIGTERM is left to its default action", "[crashhandler][regression]") {
    auto dir = fresh_dir("crash_term");

    auto r = run_child([&] {
        ::signal(SIGTERM, SIG_DFL); // Catch2 installs its own; start clean
        Arxiv::InstallCrashHandler(nullptr, dir.string());
        struct sigaction current{};
        ::sigaction(SIGTERM, nullptr, &current);
        ::_exit(current.sa_handler == SIG_DFL ? 0 : 1);
    });

    REQUIRE(WIFEXITED(r.status));
    REQUIRE(WEXITSTATUS(r.status) == 0);
    fs::remove_all(dir);
}

TEST_CASE("Crash handler: a crash writes a report and still kills the process", "[crashhandler]") {
    auto dir = fresh_dir("crash_report");
    auto replay = dir / "replay.jsonl";

    auto r = run_child([&] {
        Arxiv::ReplayRecorder recorder(replay.string());
        recorder.RecordEvent("before_the_crash");
        Arxiv::InstallCrashHandler(&recorder, dir.string());
        ::raise(SIGSEGV);
    });
    auto report = only_report(dir);

    SECTION("killed by the original signal") {
        REQUIRE(WIFSIGNALED(r.status));
        REQUIRE(WTERMSIG(r.status) == SIGSEGV);
    }
    SECTION("report names the signal") { REQUIRE_THAT(report, ContainsSubstring("SIGSEGV")); }
    SECTION("report has a backtrace") { REQUIRE_THAT(report, ContainsSubstring("Backtrace")); }
    SECTION("report carries the end of the replay log") {
        REQUIRE_THAT(report, ContainsSubstring("before_the_crash"));
    }
    fs::remove_all(dir);
}

TEST_CASE("Crash handler: does not hang when the replay recorder is mid-write",
          "[crashhandler][regression]") {
    // The recorder holds its mutex while writing; a FIFO nobody reads fills
    // up and parks a writer thread inside that lock. A handler that asks the
    // recorder for its entries then waits forever.
    auto dir = fresh_dir("crash_busy");
    auto fifo = dir / "replay.fifo";
    REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);

    auto r = run_child([&] {
        int reader = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK); // lets the writer open
        (void)reader;
        auto* recorder = new Arxiv::ReplayRecorder(fifo.string());
        Arxiv::InstallCrashHandler(recorder, dir.string());
        std::thread([recorder] {
            const std::string chunk(4096, 'x');
            for (;;)
                recorder->RecordEvent("fill", chunk);
        }).detach();
        std::this_thread::sleep_for(300ms); // the pipe is full well before this
        ::raise(SIGABRT);
    });

    REQUIRE_FALSE(r.hung);
    REQUIRE(WIFSIGNALED(r.status));
    REQUIRE(WTERMSIG(r.status) == SIGABRT);
    fs::remove_all(dir);
}

TEST_CASE("Crash handler: restores the terminal mode it found at install",
          "[crashhandler][regression]") {
    // FTXUI switches the terminal to raw mode; a crash must not leave the
    // user's shell without echo or line editing.
    auto dir = fresh_dir("crash_tty");
    int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    REQUIRE(master >= 0);
    REQUIRE(::grantpt(master) == 0);
    REQUIRE(::unlockpt(master) == 0);
    int slave = ::open(::ptsname(master), O_RDWR | O_NOCTTY);
    REQUIRE(slave >= 0);

    termios cooked{};
    REQUIRE(::tcgetattr(slave, &cooked) == 0);
    cooked.c_lflag |= (ICANON | ECHO);
    REQUIRE(::tcsetattr(slave, TCSANOW, &cooked) == 0);

    auto r = run_child([&] {
        ::dup2(slave, STDIN_FILENO);
        ::dup2(slave, STDOUT_FILENO);
        Arxiv::InstallCrashHandler(nullptr, dir.string());
        termios raw = cooked;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
        ::tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        ::raise(SIGSEGV);
    });

    termios after{};
    REQUIRE(::tcgetattr(slave, &after) == 0);
    ::close(slave);
    ::close(master);
    fs::remove_all(dir);

    REQUIRE(WIFSIGNALED(r.status));
    REQUIRE((after.c_lflag & ICANON) != 0);
    REQUIRE((after.c_lflag & ECHO) != 0);
}
