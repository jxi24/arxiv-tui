// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <Arxiv/Ranker.hh>
#include <fixtures/test_data.hh>
#include <cmath>
#include <fstream>

using namespace Arxiv;
using namespace arxiv_tui::test::fixtures;

// ---------------------------------------------------------------------------
// Ranker — vocabulary / vectorisation
// ---------------------------------------------------------------------------
TEST_CASE("Ranker vocabulary fitting", "[ranker]") {
    Ranker ranker;

    SECTION("FitVocabulary on empty corpus does not crash") {
        REQUIRE_NOTHROW(ranker.FitVocabulary({}));
    }

    SECTION("FitVocabulary on sample articles succeeds") {
        REQUIRE_NOTHROW(ranker.FitVocabulary(sample_articles));
    }

    SECTION("IsTrained is false before training") {
        ranker.FitVocabulary(sample_articles);
        REQUIRE_FALSE(ranker.IsTrained());
    }

    SECTION("Predict returns 0 before training") {
        ranker.FitVocabulary(sample_articles);
        float score = ranker.Predict(sample_articles[0]);
        REQUIRE(score == 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Ranker — training requires MIN_TRAIN samples
// ---------------------------------------------------------------------------
TEST_CASE("Ranker training threshold", "[ranker]") {
    Ranker ranker;
    ranker.FitVocabulary(sample_articles);

    SECTION("Training with fewer than MIN_TRAIN samples leaves model untrained") {
        // Only 1 sample — below threshold
        std::vector<std::pair<Article, int>> one = {{sample_articles[0], 5}};
        ranker.Train(one);
        REQUIRE_FALSE(ranker.IsTrained());
        REQUIRE(ranker.Predict(sample_articles[0]) == 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Ranker — predictions are in [1, 5] after training
// ---------------------------------------------------------------------------
TEST_CASE("Ranker predictions in valid range", "[ranker]") {
    // Build a larger corpus so training threshold is met
    std::vector<Article> corpus;
    std::vector<std::pair<Article, int>> rated;
    auto base = sample_articles[0];
    for (int i = 0; i < 6; ++i) {
        Article a = base;
        a.link = "https://arxiv.org/abs/test." + std::to_string(i);
        a.title = "Article about topic " + std::to_string(i);
        a.abstract = "Abstract discussing methods and results " + std::to_string(i);
        corpus.push_back(a);
        rated.emplace_back(a, (i % 5) + 1);
    }

    Ranker ranker;
    ranker.FitVocabulary(corpus);
    ranker.Train(rated);

    REQUIRE(ranker.IsTrained());

    SECTION("Predicted score for seen article is in [1, 5]") {
        for (const auto &[article, _] : rated) {
            float score = ranker.Predict(article);
            REQUIRE(score >= 1.0f);
            REQUIRE(score <= 5.0f);
        }
    }

    SECTION("Predicted score for unseen article is in [1, 5]") {
        Article unseen = base;
        unseen.link = "https://arxiv.org/abs/unseen.99";
        unseen.title = "Novel approach to quantum field theory";
        unseen.abstract = "We propose a new framework for calculating scattering amplitudes.";
        float score = ranker.Predict(unseen);
        REQUIRE(score >= 1.0f);
        REQUIRE(score <= 5.0f);
    }
}

// ---------------------------------------------------------------------------
// Ranker — high-rated content scores higher than low-rated on average
// ---------------------------------------------------------------------------
TEST_CASE("Ranker learns preference signal", "[ranker]") {
    // Create articles with very distinct vocabulary so the network can learn
    std::vector<Article> corpus;
    std::vector<std::pair<Article, int>> rated;
    auto base = sample_articles[0];

    // "positive" articles: mention quantum, physics, field
    for (int i = 0; i < 4; ++i) {
        Article a = base;
        a.link = "https://arxiv.org/abs/pos." + std::to_string(i);
        a.title = "Quantum physics field theory study " + std::to_string(i);
        a.abstract = "Quantum mechanics field equations physics particles " + std::to_string(i);
        corpus.push_back(a);
        rated.emplace_back(a, 5);
    }
    // "negative" articles: mention cooking, recipes, food
    for (int i = 0; i < 4; ++i) {
        Article a = base;
        a.link = "https://arxiv.org/abs/neg." + std::to_string(i);
        a.title = "Cooking recipes food preparation " + std::to_string(i);
        a.abstract = "Recipes food cooking ingredients baking " + std::to_string(i);
        corpus.push_back(a);
        rated.emplace_back(a, 1);
    }

    Ranker ranker;
    ranker.FitVocabulary(corpus);
    ranker.Train(rated);

    REQUIRE(ranker.IsTrained());

    // New positive-style article
    Article pos_test = base;
    pos_test.link = "https://arxiv.org/abs/pos.new";
    pos_test.title = "Quantum field theory particles";
    pos_test.abstract = "Physics quantum mechanics equations particles";

    // New negative-style article
    Article neg_test = base;
    neg_test.link = "https://arxiv.org/abs/neg.new";
    neg_test.title = "Cooking food recipes";
    neg_test.abstract = "Food cooking baking ingredients";

    float pos_score = ranker.Predict(pos_test);
    float neg_score = ranker.Predict(neg_test);

    // The model should predict a higher score for the quantum/physics article
    REQUIRE(pos_score > neg_score);
}

// ---------------------------------------------------------------------------
// Ranker — boundary conditions
// TDD: these tests were written before implementing the guarded behaviour.
// ---------------------------------------------------------------------------
TEST_CASE("Ranker boundary conditions", "[ranker]") {
    SECTION("Predict before FitVocabulary returns 0 without crashing") {
        Ranker r;
        REQUIRE_NOTHROW(r.Predict(sample_articles[0]));
        REQUIRE(r.Predict(sample_articles[0]) == 0.0f);
    }

    SECTION("Train with exactly MIN_TRAIN samples succeeds") {
        std::vector<Article> corpus;
        std::vector<std::pair<Article, int>> rated;
        auto base = sample_articles[0];
        for (int i = 0; i < Ranker::MIN_TRAIN; ++i) {
            Article a = base;
            a.link = "https://arxiv.org/abs/min." + std::to_string(i);
            a.title = "Min train article " + std::to_string(i);
            corpus.push_back(a);
            rated.emplace_back(a, (i % 5) + 1);
        }
        Ranker r;
        r.FitVocabulary(corpus);
        r.Train(rated);
        REQUIRE(r.IsTrained());
    }

    SECTION("Train with MIN_TRAIN - 1 samples leaves model untrained") {
        std::vector<Article> corpus;
        std::vector<std::pair<Article, int>> rated;
        auto base = sample_articles[0];
        for (int i = 0; i < Ranker::MIN_TRAIN - 1; ++i) {
            Article a = base;
            a.link = "https://arxiv.org/abs/below." + std::to_string(i);
            corpus.push_back(a);
            rated.emplace_back(a, (i % 5) + 1);
        }
        Ranker r;
        r.FitVocabulary(corpus);
        r.Train(rated);
        REQUIRE_FALSE(r.IsTrained());
    }
}

// ---------------------------------------------------------------------------
// Ranker — persistence (Save / Load)
// ---------------------------------------------------------------------------

// Helper: build a trained ranker with 5 rated articles
static Ranker make_trained_ranker() {
    auto base = sample_articles[0];
    std::vector<Article> corpus;
    std::vector<std::pair<Article, int>> rated;
    for (int i = 0; i < 5; ++i) {
        Article a = base;
        a.link    = "https://arxiv.org/abs/pers." + std::to_string(i);
        a.title   = "Persistence test article " + std::to_string(i);
        a.abstract = "Abstract about topic " + std::to_string(i);
        corpus.push_back(a);
        rated.emplace_back(a, (i % 5) + 1);
    }
    Ranker r;
    r.FitVocabulary(corpus);
    r.Train(rated);
    return r;
}

TEST_CASE("Ranker persistence", "[ranker]") {
    SECTION("Save/Load round-trip preserves trained state and predictions") {
        Ranker trained = make_trained_ranker();
        REQUIRE(trained.IsTrained());

        const std::string path = "/tmp/arxiv_tui_test_ranker.bin";
        REQUIRE(trained.Save(path));

        Ranker loaded;
        REQUIRE(loaded.Load(path));
        REQUIRE(loaded.IsTrained());

        // Predictions must match within floating-point tolerance
        Article probe = sample_articles[0];
        probe.link    = "https://arxiv.org/abs/pers.0";
        probe.title   = "Persistence test article 0";
        float original = trained.Predict(probe);
        float reloaded = loaded.Predict(probe);
        REQUIRE(std::abs(original - reloaded) < 0.001f);
    }

    SECTION("Load from nonexistent path returns false; model is unchanged") {
        Ranker r;
        REQUIRE_FALSE(r.Load("/no/such/file/ranker.bin"));
        REQUIRE_FALSE(r.IsTrained());
    }

    SECTION("Load from a file with bad magic returns false") {
        const std::string path = "/tmp/arxiv_tui_test_bad_magic.bin";
        {
            std::ofstream f(path, std::ios::binary);
            f.write("JUNK", 4);  // wrong magic
        }
        Ranker r;
        REQUIRE_FALSE(r.Load(path));
        REQUIRE_FALSE(r.IsTrained());
    }

    SECTION("Load from a truncated file returns false; model is unchanged") {
        // Write valid magic + version but nothing else
        const std::string path = "/tmp/arxiv_tui_test_truncated.bin";
        {
            std::ofstream f(path, std::ios::binary);
            f.write("RANK", 4);
            int32_t version = 1;
            f.write(reinterpret_cast<const char*>(&version), 4);
            // Stop here — vocab_size is missing
        }
        Ranker r;
        REQUIRE_FALSE(r.Load(path));
        REQUIRE_FALSE(r.IsTrained());
    }

    SECTION("Save to unwritable path returns false") {
        Ranker trained = make_trained_ranker();
        REQUIRE_FALSE(trained.Save("/no/such/directory/ranker.bin"));
    }
}
