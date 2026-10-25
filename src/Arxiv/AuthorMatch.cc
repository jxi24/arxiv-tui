// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AuthorMatch.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string_view>
#include <vector>

namespace Arxiv {

namespace {

struct GivenName {
    std::string text; // lower-case, without dots ("wei", "j", "jean-luc")
    std::string initial;
    bool is_initial = false;
};

struct ParsedName {
    std::vector<GivenName> given; // first, then middle names
    std::string last;             // lower-case, particles included ("van der berg")
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// First UTF-8 character (so "É" and "Ê" are different initials).
std::string first_char(const std::string& s) {
    if (s.empty())
        return s;
    std::size_t n = 1;
    while (n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
        ++n;
    return s.substr(0, n);
}

bool is_particle(const std::string& t) {
    static constexpr std::array<std::string_view, 21> particles = {
        "van", "von", "der", "den", "de",  "del", "della", "di",  "da",  "du", "le",
        "la",  "dos", "das", "ten", "ter", "al",  "el",    "bin", "ibn", "st."};
    return std::find(particles.begin(), particles.end(), t) != particles.end();
}

bool is_suffix(const std::string& t) {
    static constexpr std::array<std::string_view, 7> suffixes = {
        "jr", "jr.", "sr", "sr.", "ii", "iii", "iv"};
    return std::find(suffixes.begin(), suffixes.end(), t) != suffixes.end();
}

GivenName given_name(const std::string& token) {
    GivenName g;
    for (char c : token)
        if (c != '.')
            g.text += c;
    g.initial = first_char(g.text);
    // "J." or "J" or "J.-L." (hyphenated initials) are initials.
    g.is_initial = token.find('.') != std::string::npos || g.text.size() == g.initial.size();
    return g;
}

ParsedName parse(const std::string& name) {
    std::vector<std::string> tokens;
    std::istringstream in(lower(name));
    for (std::string t; in >> t;) {
        // "j.a." -> "j." "a." (initials written together)
        bool joined_initials = t.size() >= 4 && t.size() % 2 == 0;
        for (std::size_t i = 0; joined_initials && i < t.size(); i += 2)
            joined_initials = std::isalpha(static_cast<unsigned char>(t[i])) && t[i + 1] == '.';
        if (joined_initials) {
            for (std::size_t i = 0; i < t.size(); i += 2)
                tokens.push_back(t.substr(i, 2));
        } else if (!is_suffix(t)) {
            tokens.push_back(t);
        }
    }
    ParsedName p;
    if (tokens.empty())
        return p;
    std::size_t last_start = tokens.size() - 1;
    while (last_start > 1 && is_particle(tokens[last_start - 1]))
        --last_start;
    for (std::size_t i = last_start; i < tokens.size(); ++i)
        p.last += (p.last.empty() ? "" : " ") + tokens[i];
    for (std::size_t i = 0; i < last_start; ++i)
        p.given.push_back(given_name(tokens[i]));
    return p;
}

// Two given names are compatible when their initials agree and, if both
// are spelled out, the names themselves agree.
bool compatible(const GivenName& a, const GivenName& b) {
    if (a.initial != b.initial)
        return false;
    return a.is_initial || b.is_initial || a.text == b.text;
}

} // namespace

bool AuthorMatches(const std::string& followed, const std::string& author) {
    const ParsedName f = parse(followed);
    const ParsedName a = parse(author);
    if (f.last.empty() || f.last != a.last)
        return false;
    if (f.given.empty())
        return true; // following a last name alone
    if (a.given.empty())
        return false;
    if (!compatible(f.given[0], a.given[0]))
        return false;
    // Middle names count only where both names give one.
    const std::size_t shared = std::min(f.given.size(), a.given.size());
    for (std::size_t i = 1; i < shared; ++i)
        if (!compatible(f.given[i], a.given[i]))
            return false;
    return true;
}

} // namespace Arxiv
