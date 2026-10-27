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

TEST_CASE("Utf8::Truncate cuts at a character boundary", "[utf8]") {
    // The article list cut titles and author names by bytes, leaving half a
    // character before the "…" ("Schr\xC3…").
    using Arxiv::Utf8::Truncate;
    SECTION("fits: unchanged") {
        REQUIRE(Truncate("Schr\xC3\xB6"
                         "dinger",
                         20) == "Schr\xC3\xB6"
                                "dinger");
    }
    SECTION("cut with an ellipsis, counting characters") {
        REQUIRE(Truncate("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 3) == "\xC3\xA9\xC3\xA9\xE2\x80\xA6");
    }
    SECTION("zero width") { REQUIRE(Truncate("abc", 0) == ""); }
}

TEST_CASE("Utf8::Rotate starts at a character boundary", "[utf8]") {
    // The scrolling title rotated by a byte offset.
    using Arxiv::Utf8::Rotate;
    SECTION("by characters") {
        REQUIRE(Rotate("\xC3\xA9"
                       "ab",
                       1,
                       "|") == "ab|\xC3\xA9");
    }
    SECTION("wraps around") { REQUIRE(Rotate("abc", 4, "|") == "bc|a"); }
    SECTION("empty") { REQUIRE(Rotate("", 3, "|") == ""); }
}
