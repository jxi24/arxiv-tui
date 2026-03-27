#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <Arxiv/Ranker.hh>
#include <fixtures/test_data.hh>

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
