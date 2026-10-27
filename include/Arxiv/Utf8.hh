// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>

namespace Arxiv::Utf8 {

/// Remove the last character of UTF-8 text (all of its bytes), for Backspace.
/// Stray continuation bytes (invalid text) go one at a time.
inline void PopBack(std::string& s) {
    if (s.empty())
        return;
    auto continuation = [&](std::size_t i) {
        return (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80;
    };
    std::size_t start = s.size() - 1;
    // A character is a lead byte plus up to three continuation bytes.
    while (start > 0 && continuation(start) && s.size() - start < 4)
        --start;
    const auto lead = static_cast<unsigned char>(s[start]);
    if (continuation(start) || lead < 0xC0)
        start = s.size() - 1; // no lead byte owns the tail: drop one byte
    s.erase(start);
}

} // namespace Arxiv::Utf8
