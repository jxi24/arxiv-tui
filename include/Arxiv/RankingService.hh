// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Arxiv/Article.hh"
#include "Arxiv/Ranker.hh"

#include <atomic>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Arxiv {

/// Owns the ranking model: restoring and saving it, retraining it on a
/// background thread, the interest keywords and scoring. Thread-safe.
/// AppCore decides when to retrain (ratings since the last training) and
/// what each view shows.
class RankingService {
  public:
    using Rated = std::vector<std::pair<Article, int>>;

    /// `model_path` empty: the model is never saved or loaded.
    explicit RankingService(std::string model_path = "");
    ~RankingService(); // waits for a running training
    RankingService(const RankingService&) = delete;
    RankingService& operator=(const RankingService&) = delete;

    /// Restore the saved model, or, if there is none, train one on `rated`
    /// (when non-empty) and save it.
    void LoadOrTrain(const std::vector<Article>& corpus, const Rated& rated);

    /// Retrain on a background thread from this snapshot: `warm_start`
    /// continues from the current weights, otherwise vocabulary and weights
    /// are rebuilt. `on_done` runs on the training thread once the new model
    /// is in use. A training still running is waited for first.
    void StartTraining(std::vector<Article> corpus,
                       Rated rated,
                       bool warm_start,
                       std::function<void()> on_done);
    void WaitForTraining();
    bool IsTraining() const { return m_training.load(); }

    bool IsTrained() const;
    /// Ratings or keywords: anything to rank by.
    bool CanRank() const;

    /// Interest keywords; they stay fitted across retraining.
    void SetKeywords(std::vector<std::string> keywords);
    std::vector<std::string> Keywords() const;

    /// Blended model/keyword score in [1, 5], or 0 with nothing to rank by.
    float Score(const Article& article) const;
    /// Each article with its score (one lock for the whole batch).
    std::vector<std::pair<float, Article>> ScoreAll(std::vector<Article> articles) const;
    /// Highest score first (stable), keeping those scoring at least
    /// `min_score`. Each article is scored once.
    std::vector<Article> Rank(std::vector<Article> articles,
                              float min_score = std::numeric_limits<float>::lowest()) const;

  private:
    // Put `model` in use, with the current keywords fitted. Caller holds m_mutex.
    void Install(Ranker model);

    const std::string m_model_path;
    mutable std::mutex m_mutex; // guards m_ranker and m_keywords
    Ranker m_ranker;
    std::vector<std::string> m_keywords;
    std::thread m_thread;
    std::atomic<bool> m_training{false};
};

} // namespace Arxiv
