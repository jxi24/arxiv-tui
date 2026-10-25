// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Paths.hh"

#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>

using Arxiv::Paths;
namespace fs = std::filesystem;

namespace {

// A fake environment so tests never depend on the developer's XDG setup.
struct FakeEnv {
    std::map<std::string, std::string> vars;
    const char* operator()(const char* name) const {
        auto it = vars.find(name);
        return it == vars.end() ? nullptr : it->second.c_str();
    }
};

} // namespace

TEST_CASE("Paths regression: an unwritable install prefix falls back to the XDG defaults",
          "[paths][regression]") {
    // A default build bakes in /usr/local/share and /usr/local/var, which a
    // normal user cannot write; startup used to throw creating them.
    FakeEnv env{{{"HOME", "/home/alice"}}};
    auto unusable = [](const fs::path&) { return false; };

    Paths p = Paths::Resolve(env, "/usr/local/share", "/usr/local/var", unusable);

    SECTION("data") { REQUIRE(p.data_dir == fs::path("/home/alice/.local/share/arxiv-tui")); }
    SECTION("state") { REQUIRE(p.state_dir == fs::path("/home/alice/.local/state/arxiv-tui")); }
}
