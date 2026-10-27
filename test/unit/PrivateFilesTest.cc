// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// The replay log (searches, notes, keywords, paths), the log and the database
// (notes, ratings) were created 0644 in 0755 directories: readable by other
// users where home directories are 0755. Their descriptors were also
// inherited by xdg-open, the browser, tar and the clipboard tool.

#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Logging.hh"
#include "Arxiv/Paths.hh"
#include "Arxiv/Process.hh"
#include "Arxiv/Replay.hh"

#include <spdlog/spdlog.h>

#include <catch2/catch_test_macros.hpp>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
fs::path fresh_dir() {
    static int counter = 0;
    auto dir = fs::temp_directory_path() /
               ("arxiv_private_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

unsigned mode_of(const fs::path& p) {
    struct stat st{};
    ::stat(p.c_str(), &st);
    return st.st_mode & 0777;
}

// Whether this process holds `path` open without close-on-exec.
bool open_without_cloexec(const fs::path& path) {
    for (const auto& entry : fs::directory_iterator("/proc/self/fd")) {
        std::error_code ec;
        if (fs::read_symlink(entry.path(), ec) != path)
            continue;
        const int fd = std::stoi(entry.path().filename().string());
        if ((::fcntl(fd, F_GETFD) & FD_CLOEXEC) == 0)
            return true;
    }
    return false;
}
} // namespace

TEST_CASE("The replay log is private", "[security][private]") {
    const auto dir = fresh_dir();
    const auto file = dir / "replay.jsonl";
    Arxiv::ReplayRecorder recorder(file.string());
    recorder.RecordEvent("x");

    SECTION("mode 0600") { REQUIRE(mode_of(file) == 0600); }
    SECTION("not inherited by child processes") { REQUIRE_FALSE(open_without_cloexec(file)); }
    fs::remove_all(dir);
}

TEST_CASE("A new database file is private", "[security][private]") {
    const auto dir = fresh_dir();
    const auto file = dir / "articles.db";
    {
        Arxiv::DatabaseManager db(file.string());
    }
    REQUIRE(mode_of(file) == 0600);
    fs::remove_all(dir);
}

TEST_CASE("The log file is private", "[security][private]") {
    const auto dir = fresh_dir();
    const auto file = dir / "arxiv_tui.log";
    spdlog::drop("private_log_test"); // each SECTION runs the body again
    auto logger = Arxiv::MakeFileLogger("private_log_test", file, false);
    logger->info("hello");
    logger->flush();

    SECTION("mode 0600") { REQUIRE(mode_of(file) == 0600); }
    SECTION("not inherited by child processes") { REQUIRE_FALSE(open_without_cloexec(file)); }
    spdlog::drop("private_log_test");
    fs::remove_all(dir);
}

TEST_CASE("New data and state directories are private", "[security][private][paths]") {
    const auto root = fresh_dir();
    Arxiv::Paths p;
    p.config_file = root / "config" / "arxiv-tui" / "config.yml";
    p.data_dir = root / "data" / "arxiv-tui";
    p.state_dir = root / "state" / "arxiv-tui";

    p.CreateDirectories();

    REQUIRE(mode_of(p.data_dir) == 0700);
    REQUIRE(mode_of(p.state_dir) == 0700);
    REQUIRE(fs::is_directory(p.config_file.parent_path()));
    fs::remove_all(root);
}

TEST_CASE("Child processes do not inherit our descriptors", "[security][process]") {
    const int fd = ::open("/dev/null", O_RDONLY); // no O_CLOEXEC on purpose
    REQUIRE(fd > 2);
    const std::string probe = "test -e /proc/self/fd/" + std::to_string(fd);

    const int rc = Arxiv::Process::RunAndWait({"sh", "-c", probe});

    ::close(fd);
    REQUIRE(rc == 1); // the descriptor is not there
}
