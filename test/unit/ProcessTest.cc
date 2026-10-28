// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Process.hh"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>

using Arxiv::Process::RunAndWait;
using Arxiv::Process::SpawnDetached;
namespace fs = std::filesystem;

// External programs (xdg-open, tar) are launched with an argument vector and
// no shell, so article links and paths can never be interpreted as commands.

TEST_CASE("Process::RunAndWait returns the program's exit status", "[process]") {
    REQUIRE(RunAndWait({"sh", "-c", "exit 3"}) == 3);
}

TEST_CASE("Process::RunAndWait passes arguments literally, without a shell", "[process]") {
    const std::string tricky = "a\"b; touch /tmp/arxiv_should_not_exist; $(echo c)";
    REQUIRE(RunAndWait({"test", tricky, "=", tricky}) == 0);
    REQUIRE_FALSE(fs::exists("/tmp/arxiv_should_not_exist"));
}

TEST_CASE("Process::RunAndWait reports a missing program as failure", "[process]") {
    REQUIRE(RunAndWait({"arxiv-tui-no-such-program"}) != 0);
}

TEST_CASE("Process::SpawnDetached starts the program without waiting for it", "[process]") {
    auto marker = fs::temp_directory_path() / ("arxiv_spawn_marker_" + std::to_string(::getpid()));
    fs::remove(marker);

    auto t0 = std::chrono::steady_clock::now();
    REQUIRE(SpawnDetached({"sh", "-c", "sleep 1; touch \"$0\"", marker.string()}));
    REQUIRE(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(500));

    for (int i = 0; i < 50 && !fs::exists(marker); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(fs::exists(marker));
    fs::remove(marker);
}
