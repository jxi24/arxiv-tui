// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/BibTeX.hh"

#include "Arxiv/Article.hh"

#include <cctype>
#include <chrono>
#include <ctime>
#include <vector>

namespace Arxiv::BibTeX {

namespace {

bool braces_balance(const std::string& s) {
    int depth = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\') {
            ++i; // escaped character
            continue;
        }
        if (s[i] == '{')
            ++depth;
        else if (s[i] == '}' && --depth < 0)
            return false;
    }
    return depth == 0;
}

std::vector<std::string> split_authors(const std::string& authors) {
    std::vector<std::string> names;
    std::size_t start = 0;
    while (start <= authors.size()) {
        auto comma = authors.find(',', start);
        std::string name =
            authors.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        auto b = name.find_first_not_of(" \t");
        auto e = name.find_last_not_of(" \t");
        if (b != std::string::npos)
            names.push_back(name.substr(b, e - b + 1));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return names;
}

std::string utc_year(const Article& article) {
    std::time_t t = std::chrono::system_clock::to_time_t(article.date);
    std::tm tm{};
    gmtime_r(&t, &tm);
    return std::to_string(1900 + tm.tm_year);
}

} // namespace

std::string Escape(const std::string& text) {
    const bool escape_braces = !braces_balance(text);
    std::string out;
    out.reserve(text.size() + 8);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size()) {
            out += c; // keep LaTeX commands and existing escapes intact
            out += text[++i];
        } else if (c == '%' || c == '&' || c == '#' || (escape_braces && (c == '{' || c == '}'))) {
            out += '\\';
            out += c;
        } else {
            out += c;
        }
    }
    return out;
}

std::string CiteKey(const Article& article) {
    const auto names = split_authors(article.authors);
    std::string last = names.empty() ? "" : names.front();
    if (auto space = last.rfind(' '); space != std::string::npos)
        last = last.substr(space + 1);
    std::string key;
    for (char c : last)
        if (std::isalnum(static_cast<unsigned char>(c)) && static_cast<unsigned char>(c) < 0x80)
            key += c;
    if (key.empty())
        key = "arXiv";
    key += ":" + utc_year(article) + "_";
    for (char c : article.id())
        key += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-') ? c : '_';
    return key;
}

std::string Build(const Article& article) {
    const auto names = split_authors(article.authors);
    std::string authors;
    for (const auto& n : names)
        authors += (authors.empty() ? "" : " and ") + n;
    std::string primary = article.category.substr(0, article.category.find(','));
    if (auto e = primary.find_last_not_of(" \t"); e != std::string::npos)
        primary.erase(e + 1);

    std::string bib = "@article{" + CiteKey(article) + ",\n";
    bib += "  author        = {" + Escape(authors) + "},\n";
    bib += "  title         = {{" + Escape(article.title) + "}},\n";
    bib += "  eprint        = {" + article.id() + "},\n";
    bib += "  archivePrefix = {arXiv},\n";
    if (!primary.empty())
        bib += "  primaryClass  = {" + Escape(primary) + "},\n";
    bib += "  year          = {" + utc_year(article) + "},\n";
    bib += "  url           = {" + article.link + "},\n";
    bib += "}\n";
    return bib;
}

} // namespace Arxiv::BibTeX
