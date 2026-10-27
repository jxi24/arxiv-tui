// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Text fields hold UTF-8. Backspace used to remove one byte, leaving half a
// character behind ("é" then Backspace left a lone 0xC3), which then crashed
// the JSON writers.

#include "Arxiv/Utf8.hh"

#include <catch2/catch_test_macros.hpp>
#include <string>

using Arxiv::Utf8::PopBack;

TEST_CASE("Utf8::PopBack removes one whole character", "[utf8]") {
    auto popped = [](std::string s) {
        PopBack(s);
        return s;
    };
    SECTION("ASCII") { REQUIRE(popped("ab") == "a"); }
    SECTION("two-byte character") { REQUIRE(popped("a\xC3\xA9") == "a"); }
    SECTION("three-byte character") { REQUIRE(popped("a\xE2\x80\x94") == "a"); }
    SECTION("four-byte character") { REQUIRE(popped("a\xF0\x9F\x93\x9A") == "a"); }
    SECTION("empty stays empty") { REQUIRE(popped("") == ""); }
    SECTION("stray continuation bytes go one at a time") {
        REQUIRE(popped("a\x80\x80\x80\x80\x80") == "a\x80\x80\x80\x80");
    }
}
