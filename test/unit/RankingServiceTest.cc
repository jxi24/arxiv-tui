// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/RankingService.hh"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

using Arxiv::Article;
using Arxiv::RankingService;

namespace {
Article paper(int i, const std::string& topic) {
    Article a;
    a.link = "https://arxiv.org/abs/2601." + std::to_string(10000 + i);
    a.title = topic + " study " + std::to_string(i);
    a.abstract = "We measure " + topic + " in detail. The " + topic + " results are new.";
    a.authors = "A. Author";
    a.date = std::chrono::system_clock::now();
    return a;
}
} // namespace

TEST_CASE("RankingService: the first automatic retrain learns from the ratings",
          "[ranker][regression]") {
    // AppCore retrains with warm_start once retrain_interval ratings are in.
    // With no model yet that "warm start" kept the empty vocabulary: every
    // feature vector was zero and every paper got the same score.
    std::vector<Article> corpus;
    RankingService::Rated rated;
    for (int i = 0; i < 6; ++i) {
        corpus.push_back(paper(i, "neutrino oscillation"));
        rated.push_back({corpus.back(), 5});
        corpus.push_back(paper(100 + i, "galaxy cluster"));
        rated.push_back({corpus.back(), 1});
    }
    RankingService service;

    service.StartTraining(corpus, rated, /*warm_start=*/true, [] {});
    service.WaitForTraining();

    REQUIRE(service.Score(paper(50, "neutrino oscillation")) >
            service.Score(paper(150, "galaxy cluster")));
}
