#pragma once

#include "Arxiv/Article.hh"
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

namespace Arxiv {

// Lightweight article ranker using TF-IDF vectorisation and a 2-layer MLP.
// No external ML dependencies — everything is implemented in pure C++17.
//
// Architecture: input (MAX_FEATURES) → hidden (HIDDEN_SIZE, ReLU) → output (1 unit)
// Output is linearly scaled to [1.0, 5.0].
// Training uses mini-batch SGD with MSE loss.
class Ranker {
public:
    static constexpr int MAX_FEATURES = 512;
    static constexpr int HIDDEN_SIZE  = 32;
    static constexpr int MIN_TRAIN    = 3;   // minimum rated articles to enable predictions
    static constexpr int EPOCHS       = 200;
    static constexpr float LR         = 0.01f;

    Ranker();

    // Build / update IDF weights from the full article corpus.
    void FitVocabulary(const std::vector<Article> &articles);

    // Train the MLP on the set of rated articles (ratings 1-5).
    void Train(const std::vector<std::pair<Article, int>> &rated);

    // Predict a score in [1.0, 5.0] for an unrated article.
    // Returns 0.0 if the model has not been trained yet.
    float Predict(const Article &article) const;

    bool IsTrained() const { return m_trained; }

private:
    // Vocabulary: term → column index in the feature vector
    std::unordered_map<std::string, int> m_vocab;
    // IDF weights indexed by vocab column
    std::vector<float> m_idf;

    // Network weights (row-major)
    std::vector<float> m_W1; // HIDDEN_SIZE × MAX_FEATURES
    std::vector<float> m_b1; // HIDDEN_SIZE
    std::vector<float> m_W2; // HIDDEN_SIZE
    float              m_b2{0.0f};

    bool m_trained{false};

    // Text helpers
    static std::vector<std::string> Tokenise(const std::string &text);
    static bool IsStopWord(const std::string &word);

    // TF-IDF vectorisation
    std::vector<float> Vectorise(const Article &article) const;

    // Forward pass: returns hidden activations and final output
    float Forward(const std::vector<float> &x,
                  std::vector<float> &hidden_out) const;

    static float ReLU(float v) { return v > 0.0f ? v : 0.0f; }
    // Scale network output (unbounded) → [1.0, 5.0]
    static float ScaleOutput(float raw);
    // Inverse of ScaleOutput: map rating [1,5] → target for MSE
    static float NormaliseTarget(float rating);

    void InitWeights();
};

} // namespace Arxiv
