#include "Arxiv/Ranker.hh"
#include "Arxiv/LatexUtils.hh"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_set>

namespace Arxiv {

// ---------------------------------------------------------------------------
// Stop-word list (common English words unlikely to be informative)
// ---------------------------------------------------------------------------
static const std::unordered_set<std::string> STOP_WORDS = {
    "a","an","the","and","or","of","in","on","to","is","are","was","were",
    "be","been","being","have","has","had","do","does","did","will","would",
    "could","should","may","might","shall","can","for","from","with","this",
    "that","these","those","it","its","as","at","by","if","so","we","our",
    "us","not","no","but","than","then","when","where","which","who","how",
    "all","also","more","into","about","such","their","they","them","what",
    "each","other","some","any","both","through","during","up","down","out",
    "over","under","again","further","between","while","after","before","only",
    "own","same","too","very","s","t","just","don","now","i","you","he","she"
};

// ---------------------------------------------------------------------------
// Ranker constructor
// ---------------------------------------------------------------------------
Ranker::Ranker() {
    InitWeights();
}

void Ranker::InitWeights() {
    std::mt19937 rng(42);
    // Xavier initialisation
    float scale_1 = std::sqrt(2.0f / MAX_FEATURES);
    float scale_2 = std::sqrt(2.0f / HIDDEN_SIZE);
    std::normal_distribution<float> dist1(0.0f, scale_1);
    std::normal_distribution<float> dist2(0.0f, scale_2);

    m_W1.resize(static_cast<size_t>(HIDDEN_SIZE * MAX_FEATURES));
    m_b1.assign(static_cast<size_t>(HIDDEN_SIZE), 0.0f);
    m_W2.resize(static_cast<size_t>(HIDDEN_SIZE));
    m_b2 = 0.0f;

    for (auto &w : m_W1) w = dist1(rng);
    for (auto &w : m_W2) w = dist2(rng);
}

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------
std::vector<std::string> Ranker::Tokenise(const std::string &text) {
    std::vector<std::string> tokens;
    std::string tok;
    const std::string clean = StripLatex(text);
    for (char c : clean) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            tok += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            if (!tok.empty()) {
                if (tok.size() > 2 && !IsStopWord(tok)) {
                    tokens.push_back(tok);
                }
                tok.clear();
            }
        }
    }
    if (!tok.empty() && tok.size() > 2 && !IsStopWord(tok)) {
        tokens.push_back(tok);
    }
    return tokens;
}

bool Ranker::IsStopWord(const std::string &word) {
    return STOP_WORDS.count(word) > 0;
}

// ---------------------------------------------------------------------------
// FitVocabulary — build vocab and IDF from corpus
// ---------------------------------------------------------------------------
void Ranker::FitVocabulary(const std::vector<Article> &articles) {
    if (articles.empty()) return;

    // Count document frequency per term
    std::unordered_map<std::string, int> df;
    for (const auto &a : articles) {
        auto tokens = Tokenise(a.title + " " + a.abstract);
        std::unordered_set<std::string> seen(tokens.begin(), tokens.end());
        for (const auto &t : seen) {
            df[t]++;
        }
    }

    // Sort terms by document frequency (descending) and keep top MAX_FEATURES
    std::vector<std::pair<int, std::string>> sorted_df;
    sorted_df.reserve(df.size());
    for (const auto &[term, count] : df) {
        sorted_df.emplace_back(count, term);
    }
    std::sort(sorted_df.rbegin(), sorted_df.rend());

    int vocab_size = std::min(static_cast<int>(sorted_df.size()), MAX_FEATURES);
    m_vocab.clear();
    m_idf.assign(static_cast<size_t>(MAX_FEATURES), 0.0f);

    float N = static_cast<float>(articles.size());
    for (int i = 0; i < vocab_size; ++i) {
        const auto &[count, term] = sorted_df[static_cast<size_t>(i)];
        m_vocab[term] = i;
        m_idf[static_cast<size_t>(i)] = std::log((N + 1.0f) / (static_cast<float>(count) + 1.0f)) + 1.0f;
    }

    spdlog::info("[Ranker]: Vocabulary fitted with {} terms from {} articles",
                 vocab_size, articles.size());
}

// ---------------------------------------------------------------------------
// Vectorise — produce a normalised TF-IDF feature vector
// ---------------------------------------------------------------------------
std::vector<float> Ranker::Vectorise(const Article &article) const {
    std::vector<float> vec(static_cast<size_t>(MAX_FEATURES), 0.0f);
    if (m_vocab.empty()) return vec;

    auto tokens = Tokenise(article.title + " " + article.title + " " + article.abstract);
    // title repeated to give it more weight
    std::unordered_map<std::string, int> tf;
    for (const auto &t : tokens) tf[t]++;

    float norm = 0.0f;
    for (const auto &[term, count] : tf) {
        auto it = m_vocab.find(term);
        if (it == m_vocab.end()) continue;
        int idx = it->second;
        float val = static_cast<float>(count) * m_idf[static_cast<size_t>(idx)];
        vec[static_cast<size_t>(idx)] = val;
        norm += val * val;
    }
    if (norm > 0.0f) {
        norm = std::sqrt(norm);
        for (auto &v : vec) v /= norm;
    }
    return vec;
}

// ---------------------------------------------------------------------------
// Forward pass
// ---------------------------------------------------------------------------
float Ranker::Forward(const std::vector<float> &x,
                      std::vector<float> &hidden_out) const {
    hidden_out.assign(static_cast<size_t>(HIDDEN_SIZE), 0.0f);

    // Hidden layer: h = ReLU(W1 * x + b1)
    for (int j = 0; j < HIDDEN_SIZE; ++j) {
        float sum = m_b1[static_cast<size_t>(j)];
        for (int k = 0; k < MAX_FEATURES; ++k) {
            sum += m_W1[static_cast<size_t>(j * MAX_FEATURES + k)] * x[static_cast<size_t>(k)];
        }
        hidden_out[static_cast<size_t>(j)] = ReLU(sum);
    }

    // Output layer: y = W2 · h + b2
    float out = m_b2;
    for (int j = 0; j < HIDDEN_SIZE; ++j) {
        out += m_W2[static_cast<size_t>(j)] * hidden_out[static_cast<size_t>(j)];
    }
    return out;
}

// ---------------------------------------------------------------------------
// Scaling helpers
// ---------------------------------------------------------------------------
float Ranker::ScaleOutput(float raw) {
    // Map unbounded raw output through sigmoid to (0,1) then scale to [1,5]
    float sig = 1.0f / (1.0f + std::exp(-raw));
    return 1.0f + 4.0f * sig;
}

float Ranker::NormaliseTarget(float rating) {
    // Inverse of ScaleOutput: map rating [1,5] → logit space target
    // We use the normalised target t = (rating - 1) / 4 in (0,1)
    // and then the logit for the sigmoid
    float t = (rating - 1.0f) / 4.0f;
    t = std::max(0.01f, std::min(0.99f, t));
    return std::log(t / (1.0f - t));
}

// ---------------------------------------------------------------------------
// Train — SGD on the rated articles
// ---------------------------------------------------------------------------
void Ranker::Train(const std::vector<std::pair<Article, int>> &rated, bool warm_start) {
    if (static_cast<int>(rated.size()) < MIN_TRAIN) {
        spdlog::info("[Ranker]: Not enough rated articles to train ({} < {})",
                     rated.size(), MIN_TRAIN);
        return;
    }

    if (!warm_start) {
        // Cold start: re-initialise weights before training.
        InitWeights();
    } else {
        spdlog::info("[Ranker]: Warm-start — continuing from existing weights");
    }

    // Pre-vectorise all training samples
    std::vector<std::vector<float>> X;
    std::vector<float> y_target;
    X.reserve(rated.size());
    y_target.reserve(rated.size());
    for (const auto &[article, rating] : rated) {
        X.push_back(Vectorise(article));
        y_target.push_back(NormaliseTarget(static_cast<float>(rating)));
    }

    const int n = static_cast<int>(X.size());

    // Gradient buffers
    std::vector<float> dW1(m_W1.size(), 0.0f);
    std::vector<float> db1(m_b1.size(), 0.0f);
    std::vector<float> dW2(m_W2.size(), 0.0f);
    float db2 = 0.0f;

    for (int epoch = 0; epoch < EPOCHS; ++epoch) {
        float total_loss = 0.0f;

        // Zero gradients
        std::fill(dW1.begin(), dW1.end(), 0.0f);
        std::fill(db1.begin(), db1.end(), 0.0f);
        std::fill(dW2.begin(), dW2.end(), 0.0f);
        db2 = 0.0f;

        for (int i = 0; i < n; ++i) {
            const auto &x = X[static_cast<size_t>(i)];
            float t = y_target[static_cast<size_t>(i)];

            // Forward
            std::vector<float> h;
            float out = Forward(x, h);

            // MSE loss
            float err = out - t;
            total_loss += err * err;

            // Backprop output layer
            float d_out = 2.0f * err / static_cast<float>(n);
            db2 += d_out;
            for (int j = 0; j < HIDDEN_SIZE; ++j) {
                dW2[static_cast<size_t>(j)] += d_out * h[static_cast<size_t>(j)];
            }

            // Backprop hidden layer
            for (int j = 0; j < HIDDEN_SIZE; ++j) {
                float d_h = d_out * m_W2[static_cast<size_t>(j)];
                // ReLU derivative
                if (h[static_cast<size_t>(j)] <= 0.0f) continue;
                db1[static_cast<size_t>(j)] += d_h;
                for (int k = 0; k < MAX_FEATURES; ++k) {
                    dW1[static_cast<size_t>(j * MAX_FEATURES + k)] +=
                        d_h * x[static_cast<size_t>(k)];
                }
            }
        }

        // SGD update
        for (size_t idx = 0; idx < m_W1.size(); ++idx) m_W1[idx] -= LR * dW1[idx];
        for (size_t idx = 0; idx < m_b1.size(); ++idx) m_b1[idx] -= LR * db1[idx];
        for (size_t idx = 0; idx < m_W2.size(); ++idx) m_W2[idx] -= LR * dW2[idx];
        m_b2 -= LR * db2;

        if (epoch % 50 == 0) {
            spdlog::debug("[Ranker]: Epoch {} — MSE loss = {:.4f}",
                          epoch, total_loss / static_cast<float>(n));
        }
    }

    m_trained = true;
    spdlog::info("[Ranker]: Training complete on {} samples", n);
}

// ---------------------------------------------------------------------------
// Predict
// ---------------------------------------------------------------------------
float Ranker::Predict(const Article &article) const {
    if (!m_trained) return 0.0f;
    auto x = Vectorise(article);
    std::vector<float> h;
    float raw = Forward(x, h);
    return ScaleOutput(raw);
}

// ---------------------------------------------------------------------------
// Persistence helpers
// ---------------------------------------------------------------------------
// Binary format:
//   [4 bytes] magic "RANK"
//   [4 bytes] int32 version (currently 1)
//   [4 bytes] int32 vocab_size
//   for each vocab entry:
//     [4 bytes] int32 term_len
//     [term_len bytes] term chars
//     [4 bytes] int32 column_index
//   [MAX_FEATURES * 4 bytes] IDF weights
//   [HIDDEN_SIZE * MAX_FEATURES * 4 bytes] W1
//   [HIDDEN_SIZE * 4 bytes] b1
//   [HIDDEN_SIZE * 4 bytes] W2
//   [4 bytes] b2
//   [4 bytes] int32 trained flag (0 or 1)

static void write_i32(std::ofstream &f, int32_t v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
static void write_f32(std::ofstream &f, float v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
static bool read_i32(std::ifstream &f, int32_t &v) {
    return static_cast<bool>(f.read(reinterpret_cast<char*>(&v), sizeof(v)));
}
static bool read_f32(std::ifstream &f, float &v) {
    return static_cast<bool>(f.read(reinterpret_cast<char*>(&v), sizeof(v)));
}

bool Ranker::Save(const std::string &path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        spdlog::error("[Ranker]: Cannot open '{}' for writing", path);
        return false;
    }

    // Magic + version
    f.write("RANK", 4);
    write_i32(f, 1);

    // Vocabulary
    write_i32(f, static_cast<int32_t>(m_vocab.size()));
    for (const auto &[term, idx] : m_vocab) {
        write_i32(f, static_cast<int32_t>(term.size()));
        f.write(term.data(), static_cast<std::streamsize>(term.size()));
        write_i32(f, static_cast<int32_t>(idx));
    }

    // IDF, weights, biases
    for (float v : m_idf)  write_f32(f, v);
    for (float v : m_W1)   write_f32(f, v);
    for (float v : m_b1)   write_f32(f, v);
    for (float v : m_W2)   write_f32(f, v);
    write_f32(f, m_b2);
    write_i32(f, m_trained ? 1 : 0);

    if (!f) {
        spdlog::error("[Ranker]: Write error while saving to '{}'", path);
        return false;
    }
    spdlog::info("[Ranker]: Model saved to '{}'", path);
    return true;
}

bool Ranker::Load(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        spdlog::debug("[Ranker]: No saved model at '{}'", path);
        return false;
    }

    // Magic
    char magic[4]{};
    if (!f.read(magic, 4) || std::strncmp(magic, "RANK", 4) != 0) {
        spdlog::error("[Ranker]: Invalid magic in '{}'", path);
        return false;
    }

    // Version
    int32_t version = 0;
    if (!read_i32(f, version) || version != 1) {
        spdlog::error("[Ranker]: Unsupported model version {} in '{}'", version, path);
        return false;
    }

    // Vocabulary
    int32_t vocab_size = 0;
    if (!read_i32(f, vocab_size)) return false;
    std::unordered_map<std::string, int> vocab;
    vocab.reserve(static_cast<size_t>(vocab_size));
    for (int32_t i = 0; i < vocab_size; ++i) {
        int32_t term_len = 0;
        if (!read_i32(f, term_len) || term_len <= 0 || term_len > 256) return false;
        std::string term(static_cast<size_t>(term_len), '\0');
        if (!f.read(term.data(), term_len)) return false;
        int32_t idx = 0;
        if (!read_i32(f, idx)) return false;
        vocab[term] = static_cast<int>(idx);
    }

    // IDF
    std::vector<float> idf(static_cast<size_t>(MAX_FEATURES));
    for (auto &v : idf) if (!read_f32(f, v)) return false;

    // Weights
    std::vector<float> W1(static_cast<size_t>(HIDDEN_SIZE * MAX_FEATURES));
    std::vector<float> b1(static_cast<size_t>(HIDDEN_SIZE));
    std::vector<float> W2(static_cast<size_t>(HIDDEN_SIZE));
    float b2 = 0.0f;
    for (auto &v : W1) if (!read_f32(f, v)) return false;
    for (auto &v : b1) if (!read_f32(f, v)) return false;
    for (auto &v : W2) if (!read_f32(f, v)) return false;
    if (!read_f32(f, b2)) return false;

    int32_t trained_flag = 0;
    if (!read_i32(f, trained_flag)) return false;

    // Commit loaded state only after all reads succeeded
    m_vocab   = std::move(vocab);
    m_idf     = std::move(idf);
    m_W1      = std::move(W1);
    m_b1      = std::move(b1);
    m_W2      = std::move(W2);
    m_b2      = b2;
    m_trained = (trained_flag != 0);

    spdlog::info("[Ranker]: Model loaded from '{}'", path);
    return true;
}

// ---------------------------------------------------------------------------
// Keyword cold-start
// ---------------------------------------------------------------------------
void Ranker::FitKeywords(const std::vector<std::string> &keywords) {
    m_keywords.clear();
    m_keywords.reserve(keywords.size());
    for (const auto &kw : keywords) {
        std::string lower;
        lower.reserve(kw.size());
        for (char c : kw)
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!lower.empty())
            m_keywords.push_back(std::move(lower));
    }
    m_fit_keywords = !m_keywords.empty();
    spdlog::info("[Ranker]: Fitted {} keywords for cold-start scoring", m_keywords.size());
}

float Ranker::PredictKeyword(const Article &article) const {
    if (!m_fit_keywords) return 1.0f;

    const std::string text = StripLatex(article.title + " " + article.abstract);
    // Lower-case the article text for case-insensitive matching
    std::string lower_text;
    lower_text.reserve(text.size());
    for (char c : text)
        lower_text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    int hits = 0;
    for (const auto &kw : m_keywords) {
        if (lower_text.find(kw) != std::string::npos)
            ++hits;
    }
    // Fraction of keywords found, mapped linearly to [1.0, 5.0]
    float frac = static_cast<float>(hits) / static_cast<float>(m_keywords.size());
    return 1.0f + frac * 4.0f;
}

float Ranker::PredictBlended(const Article &article) const {
    if (!m_trained && !m_fit_keywords) return 0.0f;
    if (!m_trained) return PredictKeyword(article);
    if (!m_fit_keywords) return Predict(article);

    // Blend: 60% ML, 40% keyword
    float ml_score      = Predict(article);
    float keyword_score = PredictKeyword(article);
    return std::clamp(0.6f * ml_score + 0.4f * keyword_score, 1.0f, 5.0f);
}

} // namespace Arxiv
