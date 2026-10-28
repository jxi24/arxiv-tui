// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <string>
#include <vector>

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

/// Byte offsets where each character starts (a byte that is not a
/// continuation byte starts one).
inline std::vector<std::size_t> CharStarts(const std::string& s) {
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            starts.push_back(i);
    return starts;
}

/// At most `width` characters; text that does not fit ends in "…".
inline std::string Truncate(const std::string& s, int width) {
    if (width <= 0)
        return "";
    const auto starts = CharStarts(s);
    if (starts.size() <= static_cast<std::size_t>(width))
        return s;
    return s.substr(0, starts[static_cast<std::size_t>(width) - 1]) + "\xE2\x80\xA6"; // …
}

/// `s` starting at its `start`-th character (modulo its length), followed by
/// `gap` and the characters before it: a marquee.
inline std::string Rotate(const std::string& s, std::size_t start, const std::string& gap) {
    const auto starts = CharStarts(s);
    if (starts.empty())
        return s;
    const std::size_t at = starts[start % starts.size()];
    return s.substr(at) + gap + s.substr(0, at);
}

} // namespace Arxiv::Utf8
