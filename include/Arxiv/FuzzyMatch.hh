// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace Arxiv {
namespace FuzzyMatch {

// Returns an integer similarity score in [0, 100] between two strings.
// Uses normalised Levenshtein distance: 100 = identical, 0 = completely different.
// Comparison is case-insensitive.
namespace detail {

inline std::string lower(const std::string& s) {
    std::string out(s.size(), '\0');
    std::transform(s.begin(), s.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

// Edit distance between `pattern` and `text`. With `anywhere` set, `pattern`
// may match any substring of `text` for free (Sellers' algorithm): the first
// row is all zeros and the answer is the minimum of the last row. Two rows of
// O(|text|) memory either way.
inline std::size_t
edit_distance(const std::string& pattern, const std::string& text, bool anywhere) {
    std::vector<std::size_t> prev(text.size() + 1), cur(text.size() + 1);
    for (std::size_t j = 0; j <= text.size(); ++j)
        prev[j] = anywhere ? 0 : j;
    for (std::size_t i = 1; i <= pattern.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= text.size(); ++j) {
            std::size_t cost = (pattern[i - 1] == text[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return anywhere ? *std::min_element(prev.begin(), prev.end()) : prev[text.size()];
}

inline int percent(std::size_t distance, std::size_t length) {
    if (distance >= length)
        return 0;
    return static_cast<int>(100.0 *
                            (1.0 - static_cast<double>(distance) / static_cast<double>(length)));
}

} // namespace detail

/// Case-insensitive similarity of two whole strings, 0–100.
inline int Similarity(const std::string& a, const std::string& b) {
    if (a.empty() && b.empty())
        return 100;
    if (a.empty() || b.empty())
        return 0;
    const auto d = detail::edit_distance(detail::lower(a), detail::lower(b), false);
    return detail::percent(d, std::max(a.size(), b.size()));
}

/// True if `query` is similar to the whole of `text` or to any substring of
/// it (similarity measured against the query's length), case-insensitively.
inline bool MatchesText(const std::string& query, const std::string& text, int threshold) {
    if (query.empty())
        return true;
    if (Similarity(query, text) >= threshold)
        return true;
    const auto d = detail::edit_distance(detail::lower(query), detail::lower(text), true);
    return detail::percent(d, query.size()) >= threshold;
}

} // namespace FuzzyMatch
} // namespace Arxiv
