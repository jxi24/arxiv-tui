// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// config.yml, ranker.bin and the keywords file used to be rewritten in place:
// a write that failed half way (full disk, crash) left them truncated. Each
// test makes the write fail in a forked child by lowering RLIMIT_FSIZE, then
// checks the previous file survived.

#include "Arxiv/AtomicFile.hh"

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Ranker.hh"

#include <catch2/catch_test_macros.hpp>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "mocks/FetcherMock.hh"

namespace fs = std::filesystem;

namespace {

fs::path fresh_dir() {
    static int counter = 0;
    auto dir = fs::temp_directory_path() /
               ("arxiv_atomic_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void put(const fs::path& p, const std::string& content) {
    std::ofstream(p, std::ios::binary) << content;
}

// Run `write` in a child that may only write files up to `limit` bytes.
template <typename F> void with_file_size_limit(rlim_t limit, F&& write) {
    pid_t pid = ::fork();
    if (pid == 0) {
        ::signal(SIGXFSZ, SIG_IGN); // fail the write with EFBIG instead
        rlimit lim{limit, limit};
        ::setrlimit(RLIMIT_FSIZE, &lim);
        write();
        ::_exit(0);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
}

const std::string kOld(200, 'o'); // longer than any limit used below

} // namespace

TEST_CASE("WriteFileAtomically writes and replaces", "[atomic]") {
    auto dir = fresh_dir();
    auto path = dir / "f.txt";

    SECTION("creates the file") {
        REQUIRE(Arxiv::WriteFileAtomically(path, "new"));
        REQUIRE(slurp(path) == "new");
    }
    SECTION("replaces an existing file and leaves no temp file") {
        put(path, kOld);
        REQUIRE(Arxiv::WriteFileAtomically(path, "new"));
        REQUIRE(slurp(path) == "new");
        REQUIRE(std::distance(fs::directory_iterator(dir), fs::directory_iterator()) == 1);
    }
    SECTION("fails in a directory that does not exist") {
        REQUIRE_FALSE(Arxiv::WriteFileAtomically(dir / "missing" / "f.txt", "x"));
    }
    fs::remove_all(dir);
}

TEST_CASE("WriteFileAtomically keeps the old file when the write fails", "[atomic][regression]") {
    auto dir = fresh_dir();
    auto path = dir / "f.txt";
    put(path, kOld);

    with_file_size_limit(
        16, [&] { ::_exit(Arxiv::WriteFileAtomically(path, std::string(100, 'n')) ? 1 : 0); });

    REQUIRE(slurp(path) == kOld);
    REQUIRE(std::distance(fs::directory_iterator(dir), fs::directory_iterator()) == 1);
    fs::remove_all(dir);
}

TEST_CASE("WriteFileAtomically writes into a non-regular file in place", "[atomic][regression]") {
    // Renaming over a device or FIFO would replace it with a regular file
    // (an export to /dev/full did exactly that when run as root).
    auto dir = fresh_dir();
    auto fifo = dir / "pipe";
    REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
    int reader = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
    REQUIRE(reader >= 0);

    bool ok = Arxiv::WriteFileAtomically(fifo, "hello");
    char buf[16] = {};
    auto n = ::read(reader, buf, sizeof(buf));
    ::close(reader);

    REQUIRE(ok);
    REQUIRE(fs::is_fifo(fifo));
    REQUIRE(std::string(buf, n > 0 ? static_cast<std::size_t>(n) : 0) == "hello");
    fs::remove_all(dir);
}

TEST_CASE("WriteFileAtomically writes through a symlink", "[atomic]") {
    // A dotfiles setup links ~/.config/arxiv-tui/config.yml into a repo;
    // replacing the link with a regular file would silently fork the config.
    auto dir = fresh_dir();
    auto real = dir / "real.yml";
    auto link = dir / "link.yml";
    put(real, kOld);
    fs::create_symlink(real, link);

    REQUIRE(Arxiv::WriteFileAtomically(link, "new"));

    REQUIRE(fs::is_symlink(link));
    REQUIRE(slurp(real) == "new");
    fs::remove_all(dir);
}

TEST_CASE("Config::save_to_file keeps the old config when the write fails",
          "[atomic][config][regression]") {
    auto dir = fresh_dir();
    auto path = dir / "config.yml";
    put(path, kOld);

    with_file_size_limit(16, [&] {
        Arxiv::Config cfg;
        cfg.set_topics({"hep-ph", "hep-th", "hep-ex", "astro-ph.CO"});
        cfg.save_to_file(path.string());
    });

    REQUIRE(slurp(path) == kOld);
    fs::remove_all(dir);
}

TEST_CASE("Ranker::Save keeps the old model when the write fails", "[atomic][ranker][regression]") {
    auto dir = fresh_dir();
    auto path = dir / "ranker.bin";
    put(path, kOld);

    with_file_size_limit(10, [&] {
        Arxiv::Ranker ranker;
        ranker.Save(path.string());
    });

    REQUIRE(slurp(path) == kOld);
    fs::remove_all(dir);
}

TEST_CASE("AppCore::SaveKeywords keeps the old list when the write fails",
          "[atomic][keywords][regression]") {
    auto dir = fresh_dir();
    auto path = dir / "keywords.txt";
    put(path, kOld);
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir(dir.string());
    cfg.set_ranker_file((dir / "ranker.bin").string());
    cfg.set_keywords_file(path.string());
    Arxiv::AppCore core(cfg,
                        std::make_unique<Arxiv::DatabaseManager>(":memory:"),
                        std::make_unique<arxiv_tui::test::FetcherMock>(),
                        Arxiv::AppCore::FetchMode::Offline);

    with_file_size_limit(16, [&] {
        core.SaveKeywords({"neutrino oscillations", "dark matter", "lattice QCD", "collider"});
    });

    REQUIRE(slurp(path) == kOld);
    fs::remove_all(dir);
}
