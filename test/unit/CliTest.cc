// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Command-line parsing: unknown flags and missing values used to be ignored
// silently (a typo like --fecth started the TUI instead of fetching).

#include "Arxiv/Cli.hh"

#include "Arxiv/Version.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>
#include <vector>

using Arxiv::Cli::Options;
using Arxiv::Cli::Parse;
using Catch::Matchers::ContainsSubstring;

namespace {
Options parse(std::vector<std::string> args) {
    std::vector<const char*> argv{"arxiv-tui"};
    for (const auto& a : args)
        argv.push_back(a.c_str());
    return Parse(static_cast<int>(argv.size()), argv.data());
}
} // namespace

TEST_CASE("Cli::Parse", "[cli]") {
    SECTION("no arguments: the TUI") {
        auto o = parse({});
        REQUIRE(o.error.empty());
        REQUIRE(o.mode == Options::Mode::Tui);
    }
    SECTION("--help and -h") {
        REQUIRE(parse({"--help"}).mode == Options::Mode::Help);
        REQUIRE(parse({"-h"}).mode == Options::Mode::Help);
    }
    SECTION("--version and -V") {
        REQUIRE(parse({"--version"}).mode == Options::Mode::Version);
        REQUIRE(parse({"-V"}).mode == Options::Mode::Version);
    }
    SECTION("--fetch") { REQUIRE(parse({"--fetch"}).mode == Options::Mode::Fetch); }
    SECTION("--replay takes a file") {
        auto o = parse({"--replay", "r.jsonl"});
        REQUIRE(o.mode == Options::Mode::Replay);
        REQUIRE(o.replay_file == "r.jsonl");
    }
    SECTION("both exports together") {
        auto o = parse({"--export-today", "d.md", "--export-yaml", "d.yml"});
        REQUIRE(o.mode == Options::Mode::Export);
        REQUIRE(o.export_today == "d.md");
        REQUIRE(o.export_yaml == "d.yml");
    }
    SECTION("--trace combines with a mode") {
        auto o = parse({"--trace", "--fetch"});
        REQUIRE(o.trace);
        REQUIRE(o.mode == Options::Mode::Fetch);
    }
}

TEST_CASE("Cli::Parse rejects bad command lines", "[cli][regression]") {
    SECTION("an unknown option") {
        REQUIRE_THAT(parse({"--fecth"}).error, ContainsSubstring("--fecth"));
    }
    SECTION("a stray argument") {
        REQUIRE_THAT(parse({"articles.db"}).error, ContainsSubstring("articles.db"));
    }
    SECTION("an option missing its file") {
        REQUIRE_THAT(parse({"--replay"}).error, ContainsSubstring("--replay"));
    }
    SECTION("two modes at once") {
        // --fetch used to win silently and the export never ran.
        REQUIRE_FALSE(parse({"--fetch", "--export-yaml", "d.yml"}).error.empty());
    }
}

TEST_CASE("Cli::Usage and Cli::VersionText", "[cli]") {
    const std::string usage = Arxiv::Cli::Usage();
    for (const char* flag : {"--help",
                             "--version",
                             "--fetch",
                             "--export-today",
                             "--export-yaml",
                             "--replay",
                             "--trace"})
        REQUIRE_THAT(usage, ContainsSubstring(flag));
    REQUIRE_THAT(Arxiv::Cli::VersionText(), ContainsSubstring(std::string(Arxiv::kVersion)));
}
