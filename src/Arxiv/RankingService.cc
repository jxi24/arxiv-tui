// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/RankingService.hh"

#include <algorithm>

namespace Arxiv {

RankingService::RankingService(std::string model_path)
    : m_model_path(std::move(model_path)) {}

RankingService::~RankingService() { WaitForTraining(); }

void RankingService::Install(Ranker model) {
    m_ranker = std::move(model);
    m_ranker.FitKeywords(m_keywords); // a fresh or loaded model has none
}

void RankingService::LoadOrTrain(const std::vector<Article>& corpus, const Rated& rated) {
    Ranker model;
    if (m_model_path.empty() || !model.Load(m_model_path)) {
        if (rated.empty())
            return;
        model.FitVocabulary(corpus);
        model.Train(rated);
        if (!m_model_path.empty())
            model.Save(m_model_path);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    Install(std::move(model));
}

void RankingService::StartTraining(std::vector<Article> corpus,
                                   Rated rated,
                                   bool warm_start,
                                   std::function<void()> on_done) {
    WaitForTraining();
    Ranker seed;
    if (warm_start) {
        std::lock_guard<std::mutex> lock(m_mutex);
        seed = m_ranker; // keeps the vocabulary; SGD continues from these weights
    }
    m_training = true;
    m_thread = std::thread([this,
                            warm_start,
                            corpus = std::move(corpus),
                            rated = std::move(rated),
                            seed = std::move(seed),
                            on_done = std::move(on_done)]() mutable {
        if (!warm_start)
            seed.FitVocabulary(corpus);
        seed.Train(rated, warm_start);
        if (!m_model_path.empty())
            seed.Save(m_model_path);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Install(std::move(seed));
        }
        m_training = false;
        if (on_done)
            on_done();
    });
}

void RankingService::WaitForTraining() {
    if (m_thread.joinable())
        m_thread.join();
}

bool RankingService::IsTrained() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ranker.IsTrained();
}

bool RankingService::CanRank() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ranker.CanRank();
}

void RankingService::SetKeywords(std::vector<std::string> keywords) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keywords = std::move(keywords);
    m_ranker.FitKeywords(m_keywords);
}

std::vector<std::string> RankingService::Keywords() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_keywords;
}

float RankingService::Score(const Article& article) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ranker.PredictBlended(article);
}

std::vector<std::pair<float, Article>>
RankingService::ScoreAll(std::vector<Article> articles) const {
    std::vector<std::pair<float, Article>> scored;
    scored.reserve(articles.size());
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& a : articles)
        scored.emplace_back(m_ranker.PredictBlended(a), std::move(a));
    return scored;
}

std::vector<Article> RankingService::Rank(std::vector<Article> articles, float min_score) const {
    auto scored = ScoreAll(std::move(articles));
    std::stable_sort(scored.begin(), scored.end(), [](const auto& l, const auto& r) {
        return l.first > r.first;
    });
    std::vector<Article> out;
    out.reserve(scored.size());
    for (auto& [score, article] : scored)
        if (score >= min_score)
            out.push_back(std::move(article));
    return out;
}

} // namespace Arxiv
