// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <Arxiv/Article.hh>
#include <catch2/catch_test_macros.hpp>

using Arxiv::Article;

// ---------------------------------------------------------------------------
// Article::id — old-style identifiers carry an archive prefix with a slash
// (e.g. hep-ph/0101001), so "everything after the last slash" truncates them
// and produces a wrong PDF URL, BibTeX eprint and Obsidian file name.
// ---------------------------------------------------------------------------
TEST_CASE("Article::id keeps the archive prefix of old-style identifiers",
          "[article][regression]") {
    Article article;

    SECTION("archive without subject class") {
        article.link = "https://arxiv.org/abs/hep-ph/0101001";
        REQUIRE(article.id() == "hep-ph/0101001");
    }

    SECTION("archive with subject class") {
        article.link = "https://arxiv.org/abs/math.AG/0601001";
        REQUIRE(article.id() == "math.AG/0601001");
    }
}
