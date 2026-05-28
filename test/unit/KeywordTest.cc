// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Article.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Ranker.hh"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <string>
#include <vector>

using Arxiv::Article;
using Arxiv::Ranker;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static Article make_article(const std::string& title, const std::string& abstract_text) {
    Article a;
    a.link = "https://arxiv.org/abs/9999." + title.substr(0, 3);
    a.title = title;
    a.authors = "Test Author";
    a.abstract = abstract_text;
    a.date = std::chrono::system_clock::now();
    a.bookmarked = false;
    return a;
}

// ---------------------------------------------------------------------------
// Ranker::FitKeywords / PredictKeyword
// ---------------------------------------------------------------------------

TEST_CASE("Ranker::FitKeywords: IsFitKeywords returns false before and true after",
          "[keyword][ranker]") {
    Ranker r;
    REQUIRE_FALSE(r.IsFitKeywords());
    r.FitKeywords({"neural", "network"});
    REQUIRE(r.IsFitKeywords());
}

TEST_CASE("Ranker::PredictKeyword: returns 1.0 when no keywords fitted", "[keyword][ranker]") {
    Ranker r;
    auto art = make_article("Deep learning", "We study deep learning methods.");
    REQUIRE(r.PredictKeyword(art) == Catch::Approx(1.0f));
}

TEST_CASE("Ranker::PredictKeyword: returns higher score for matching article",
          "[keyword][ranker]") {
    Ranker r;
    r.FitKeywords({"neural", "network", "deep"});

    auto match = make_article("Deep neural network training",
                              "We train deep neural networks for classification.");
    auto no_match = make_article("Thermodynamic equilibrium",
                                 "Entropy and heat transfer in classical systems.");

    float score_match = r.PredictKeyword(match);
    float score_no_match = r.PredictKeyword(no_match);

    REQUIRE(score_match > score_no_match);
    // Both scores must be in [1, 5]
    REQUIRE(score_match >= 1.0f);
    REQUIRE(score_match <= 5.0f);
    REQUIRE(score_no_match >= 1.0f);
    REQUIRE(score_no_match <= 5.0f);
}

TEST_CASE("Ranker::PredictKeyword: single keyword hit returns score above minimum",
          "[keyword][ranker]") {
    Ranker r;
    r.FitKeywords({"quantum"});

    auto art = make_article("Quantum computing applications", "We explore quantum algorithms.");
    float score = r.PredictKeyword(art);
    REQUIRE(score > 1.0f);
    REQUIRE(score <= 5.0f);
}

// ---------------------------------------------------------------------------
// Ranker::PredictBlended
// ---------------------------------------------------------------------------

TEST_CASE("Ranker::PredictBlended: returns 0.0 when neither trained nor keywords fitted",
          "[keyword][ranker]") {
    Ranker r;
    auto art = make_article("Test", "Abstract.");
    REQUIRE(r.PredictBlended(art) == Catch::Approx(0.0f));
}

TEST_CASE("Ranker::PredictBlended: returns keyword score when not ML-trained",
          "[keyword][ranker]") {
    Ranker r;
    r.FitKeywords({"quantum"});

    auto art = make_article("Quantum entanglement", "Quantum states and measurements.");
    float blended = r.PredictBlended(art);
    float keyword = r.PredictKeyword(art);

    REQUIRE(blended == Catch::Approx(keyword));
}

TEST_CASE("Ranker::PredictBlended: in [1,5] when ML-trained and keywords fitted",
          "[keyword][ranker]") {
    Ranker r;
    r.FitKeywords({"neural", "network"});

    // Build a corpus and train
    std::vector<Article> corpus;
    for (int i = 0; i < 10; ++i) {
        corpus.push_back(
            make_article("Neural network paper " + std::to_string(i),
                         "Deep learning and neural networks for task " + std::to_string(i)));
    }
    r.FitVocabulary(corpus);

    std::vector<std::pair<Article, int>> rated;
    for (int i = 0; i < 5; ++i)
        rated.emplace_back(corpus[static_cast<size_t>(i)], (i % 5) + 1);
    r.Train(rated);

    auto art = make_article("Neural network for classification", "We apply deep neural networks.");
    float blended = r.PredictBlended(art);
    REQUIRE(blended >= 1.0f);
    REQUIRE(blended <= 5.0f);
}

// ---------------------------------------------------------------------------
// Config::keywords_file
// ---------------------------------------------------------------------------

TEST_CASE("Config::keywords_file: default is empty string", "[keyword][config]") {
    Arxiv::Config cfg;
    REQUIRE(cfg.get_keywords_file().empty());
}

TEST_CASE("Config::set_keywords_file: getter returns set value", "[keyword][config]") {
    Arxiv::Config cfg;
    cfg.set_keywords_file("/home/user/arxiv_keywords.txt");
    REQUIRE(cfg.get_keywords_file() == "/home/user/arxiv_keywords.txt");
}

// ---------------------------------------------------------------------------
// DatabaseManager: relevance_score column
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::SetRelevanceScore / GetRelevanceScore round-trip", "[keyword][db]") {
    Arxiv::DatabaseManager db(":memory:");

    Arxiv::Article art;
    art.link = "https://arxiv.org/abs/2403.99999";
    art.title = "Test";
    art.authors = "Author";
    art.abstract = "Abstract";
    art.date = std::chrono::system_clock::now();
    art.bookmarked = false;
    db.AddArticle(art);

    SECTION("GetRelevanceScore returns 0.0 by default") {
        REQUIRE(db.GetRelevanceScore(art.link) == Catch::Approx(0.0f));
    }

    SECTION("SetRelevanceScore stores and retrieves the value") {
        db.SetRelevanceScore(art.link, 3.75f);
        REQUIRE(db.GetRelevanceScore(art.link) == Catch::Approx(3.75f));
    }

    SECTION("Updating an existing score overwrites it") {
        db.SetRelevanceScore(art.link, 2.0f);
        db.SetRelevanceScore(art.link, 4.5f);
        REQUIRE(db.GetRelevanceScore(art.link) == Catch::Approx(4.5f));
    }
}
