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
inline int Similarity(const std::string& a, const std::string& b) {
    if (a.empty() && b.empty()) return 100;
    if (a.empty() || b.empty()) return 0;

    // Lowercase copies
    std::string la(a.size(), '\0'), lb(b.size(), '\0');
    std::transform(a.begin(), a.end(), la.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(b.begin(), b.end(), lb.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const std::size_t m = la.size();
    const std::size_t n = lb.size();

    // dp[i][j] = edit distance between la[0..i-1] and lb[0..j-1]
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (std::size_t i = 0; i <= m; ++i) dp[i][0] = static_cast<int>(i);
    for (std::size_t j = 0; j <= n; ++j) dp[0][j] = static_cast<int>(j);

    for (std::size_t i = 1; i <= m; ++i) {
        for (std::size_t j = 1; j <= n; ++j) {
            int cost = (la[i - 1] == lb[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i - 1][j] + 1,
                dp[i][j - 1] + 1,
                dp[i - 1][j - 1] + cost
            });
        }
    }

    int edit_distance = dp[m][n];
    int max_len = static_cast<int>(std::max(m, n));
    return static_cast<int>(100.0 * (1.0 - static_cast<double>(edit_distance) / max_len));
}

// Returns true if the query fuzzy-matches any substring of text of comparable
// length, or the whole text, with similarity >= threshold.
// Uses a character-level sliding window of the same length as the query.
inline bool MatchesText(const std::string& query, const std::string& text, int threshold) {
    if (query.empty()) return true;

    // Lower-case both for comparison
    std::string lq(query.size(), '\0'), lt(text.size(), '\0');
    std::transform(query.begin(), query.end(), lq.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(text.begin(), text.end(), lt.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Whole-string similarity
    if (Similarity(lq, lt) >= threshold) return true;

    // Sliding window: try substrings of length ±50% of query length
    const std::size_t qlen = lq.size();
    const std::size_t tlen = lt.size();
    if (tlen < qlen) return false;

    std::size_t win = qlen;
    std::size_t steps = tlen - win + 1;
    for (std::size_t i = 0; i < steps; ++i) {
        std::string sub = lt.substr(i, win);
        if (Similarity(lq, sub) >= threshold) return true;
    }
    return false;
}

} // namespace FuzzyMatch
} // namespace Arxiv
