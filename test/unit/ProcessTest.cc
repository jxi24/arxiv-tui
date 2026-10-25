// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Process.hh"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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

TEST_CASE("Process::RunWithInput feeds the text to the program's stdin", "[process]") {
    auto out = fs::temp_directory_path() / ("arxiv_input_" + std::to_string(::getpid()));
    fs::remove(out);

    REQUIRE(Arxiv::Process::RunWithInput({"tee", out.string()}, "hello\nworld") == 0);

    std::ifstream f(out);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE(content == "hello\nworld");
    fs::remove(out);
}

TEST_CASE("Process::RunWithInput survives a program that exits without reading", "[process]") {
    // Writing to a closed pipe raises SIGPIPE, which would kill the TUI.
    const std::string big(1 << 20, 'x');
    REQUIRE(Arxiv::Process::RunWithInput({"true"}, big) == 0);
}

TEST_CASE("Process::RunWithInput reports a missing program as failure", "[process]") {
    REQUIRE(Arxiv::Process::RunWithInput({"arxiv-tui-no-such-program"}, "x") != 0);
}
