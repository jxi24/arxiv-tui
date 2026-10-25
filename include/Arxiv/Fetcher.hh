// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARXIV_FETCHER
#define ARXIV_FETCHER

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Arxiv {

using time_point = std::chrono::system_clock::time_point;

struct Article;

/// Thrown by Fetch/FetchSince when arXiv could not be reached or answered
/// with an error, so callers can tell "nothing new" from "fetch failed".
struct FetchError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Fetcher {
  public:
    explicit Fetcher(const std::vector<std::string>& topics,
                     const std::string& base_path = "downloads");
    virtual ~Fetcher() = default;
    /// Today's RSS announcement. Throws FetchError if the feed is unreachable.
    virtual std::vector<Article> Fetch();
    /// Fetch all articles submitted since utc_date ("YYYY-MM-DD", inclusive
    /// of the day after utc_date) using the arXiv search API. Throws FetchError
    /// if any page fails, so a partial result is never mistaken for a full one.
    virtual std::vector<Article> FetchSince(const std::string& utc_date);
    virtual bool DownloadPaper(const std::string& paper_id, const std::string& output_path);
    /// Fetch BibTeX for an arXiv paper. Tries InspireHEP first; returns an
    /// empty string if the lookup fails (caller should generate fallback BibTeX).
    virtual std::string FetchBibTeX(const std::string& paper_id);

    /// Normalize an arXiv link to canonical form: https scheme, /abs/ (a /pdf/
    /// link and its ".pdf" are folded into it), no version suffix.
    /// e.g. "http://arxiv.org/abs/2605.28788v1" → "https://arxiv.org/abs/2605.28788"
    static std::string NormalizeLink(const std::string& link);

    /// True if `link` is an arXiv abstract or PDF URL
    /// ("https://arxiv.org/abs/<id>" or ".../pdf/<id>"), where <id> uses only
    /// the characters that appear in arXiv identifiers. Links reach file names
    /// and external programs, so anything else from a feed or an import is
    /// rejected.
    static bool IsArxivLink(const std::string& link);

    /// Concatenate today's RSS announcement with the API backfill, announcement
    /// first. The two feeds date a paper differently — RSS carries the
    /// announcement date, the API the v1 submission date — and a paper
    /// submitted a day or two ago but announced today appears in both.
    /// DatabaseManager::AddArticle upserts and never rewrites `date`, so
    /// whichever batch reaches it first fixes the date for good; the
    /// announcement has to win, or today's papers are stored dated two days ago
    /// and vanish from every recency view.
    static std::vector<Article> MergeAnnouncementFirst(std::vector<Article> announcement,
                                                       std::vector<Article> backfill);

    // Parsing helpers exposed for testing.
    std::vector<Article> ParseFeed(const std::string& xml) const;
    std::vector<Article> ParseAtomFeed(const std::string& xml) const;
    std::optional<time_point> ParseDate(const std::string& date) const;
    std::optional<time_point> ParseAtomDate(const std::string& date) const;
    std::string ReplaceLatexAccents(const std::string& text) const;
    /// Convert LaTeX formatting commands to Markdown equivalents:
    /// \textit → *x*, \textbf → **x**, \texttt → `x`, \st → ~~x~~.
    /// $...$ math spans are preserved unchanged.
    std::string LatexToMarkdown(const std::string& text) const;
    std::string ConstructPaperUrl(const std::string& paper_id, const std::string& format) const;

    /// arXiv announces papers one or more days after their submission date, so
    /// the submittedDate the API filters on lags the date a paper "comes out".
    /// FetchSince therefore starts its submittedDate window this many days
    /// *before* the requested date, so papers submitted shortly before the last
    /// fetch but announced after it are not silently dropped. Papers already in
    /// the DB are upserted without touching their date, so the overlap is harmless.
    static constexpr int announce_lag_days = 5;

    /// Compute the submittedDate query-window start for FetchSince: utc_date
    /// ("YYYY-MM-DD") shifted back by announce_lag_days, returned as
    /// "YYYY-MM-DD". Exposed for testing. Returns utc_date unchanged if it
    /// cannot be parsed.
    std::string FetchSinceWindowStart(const std::string& utc_date) const;

  private:
    std::vector<std::string> m_topics;

    std::optional<std::string> FetchFeeds();
    std::filesystem::path base_path;
};

} // namespace Arxiv

#endif
