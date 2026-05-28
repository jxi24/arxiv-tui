// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/Article.hh"
#include "Arxiv/FuzzyMatch.hh"

#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"
#include "fixtures/test_data.hh"

using namespace Catch::Matchers;
using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock         = arxiv_tui::test::FetcherMock;

// ---------------------------------------------------------------------------
// FuzzyMatch::Similarity unit tests
// ---------------------------------------------------------------------------

TEST_CASE("FuzzyMatch::Similarity: identical strings score 100", "[fuzzy]") {
    REQUIRE(Arxiv::FuzzyMatch::Similarity("hello", "hello") == 100);
}

TEST_CASE("FuzzyMatch::Similarity: empty strings", "[fuzzy]") {
    REQUIRE(Arxiv::FuzzyMatch::Similarity("", "") == 100);
    REQUIRE(Arxiv::FuzzyMatch::Similarity("hello", "") == 0);
    REQUIRE(Arxiv::FuzzyMatch::Similarity("", "hello") == 0);
}

TEST_CASE("FuzzyMatch::Similarity: one typo returns high score", "[fuzzy]") {
    // "nural" vs "neural" — one character off
    int score = Arxiv::FuzzyMatch::Similarity("nural", "neural");
    REQUIRE(score >= 70);
    REQUIRE(score < 100);
}

TEST_CASE("FuzzyMatch::Similarity: completely different strings score low", "[fuzzy]") {
    int score = Arxiv::FuzzyMatch::Similarity("quantum", "apple");
    REQUIRE(score < 50);
}

TEST_CASE("FuzzyMatch::Similarity: shorter string vs longer string", "[fuzzy]") {
    // "neural" vs "neural networks" — edit distance = 9 (add " networks"),
    // max_len = 15, score = 100*(1-9/15) = 40. Just verify it's non-zero.
    int score = Arxiv::FuzzyMatch::Similarity("neural", "neural networks");
    REQUIRE(score > 0);
    REQUIRE(score < 100);
}

TEST_CASE("FuzzyMatch::Similarity: case-insensitive", "[fuzzy]") {
    REQUIRE(Arxiv::FuzzyMatch::Similarity("Neural", "neural") == 100);
}

// ---------------------------------------------------------------------------
// AppCore::FuzzySearchArticles
// ---------------------------------------------------------------------------

static std::unique_ptr<Arxiv::AppCore> make_core(
    DatabaseManagerMock*& db_out,
    FetcherMock*&         fetcher_out)
{
    auto db_ptr  = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    db_out      = db_ptr.get();
    fetcher_out = fet_ptr.get();
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    return std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));
}

TEST_CASE("AppCore::FuzzySearchArticles: returns exact matches", "[fuzzy][appcore]") {
    DatabaseManagerMock* db     = nullptr;
    FetcherMock*         fetcher = nullptr;
    auto core = make_core(db, fetcher);

    auto articles = arxiv_tui::test::fixtures::sample_articles;
    ALLOW_CALL(*db, GetRecent(ANY(int))).RETURN(articles);

    // "Sample Article Title" is an exact match
    auto results = core->FuzzySearchArticles("Sample Article Title", 80);
    REQUIRE(!results.empty());
    REQUIRE(results[0].title == "Sample Article Title");
}

TEST_CASE("AppCore::FuzzySearchArticles: returns near-match with one typo", "[fuzzy][appcore]") {
    DatabaseManagerMock* db     = nullptr;
    FetcherMock*         fetcher = nullptr;
    auto core = make_core(db, fetcher);

    auto articles = arxiv_tui::test::fixtures::sample_articles;
    ALLOW_CALL(*db, GetRecent(ANY(int))).RETURN(articles);

    // "Sampl Article" — missing 'e', should still match at threshold 70
    auto results = core->FuzzySearchArticles("Sampl Article", 70);
    REQUIRE(!results.empty());
}

TEST_CASE("AppCore::FuzzySearchArticles: high threshold filters out weak matches", "[fuzzy][appcore]") {
    DatabaseManagerMock* db     = nullptr;
    FetcherMock*         fetcher = nullptr;
    auto core = make_core(db, fetcher);

    auto articles = arxiv_tui::test::fixtures::sample_articles;
    ALLOW_CALL(*db, GetRecent(ANY(int))).RETURN(articles);

    // "completely unrelated" — should not match "Sample Article Title" at high threshold
    auto results = core->FuzzySearchArticles("completely unrelated xyz", 95);
    // Either empty or doesn't contain the sample article
    for (const auto& a : results) {
        REQUIRE_THAT(a.title, !ContainsSubstring("Sample Article Title"));
    }
}

TEST_CASE("AppCore::FuzzySearchArticles: empty corpus returns empty", "[fuzzy][appcore]") {
    DatabaseManagerMock* db     = nullptr;
    FetcherMock*         fetcher = nullptr;
    auto core = make_core(db, fetcher);

    ALLOW_CALL(*db, GetRecent(ANY(int))).RETURN(std::vector<Arxiv::Article>{});

    auto results = core->FuzzySearchArticles("anything", 80);
    REQUIRE(results.empty());
}
