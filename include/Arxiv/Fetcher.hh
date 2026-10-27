// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARXIV_FETCHER
#define ARXIV_FETCHER

#include "Arxiv/HttpClient.hh"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Arxiv {

using time_point = std::chrono::system_clock::time_point;

struct Article;

/// Thrown by Fetch/FetchSince when arXiv could not be reached or answered
/// with an error, so callers can tell "nothing new" from "fetch failed".
struct FetchError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Receives a downloaded PDF in chunks. The bytes go to a temporary file
/// beside `target`; Commit() checks the result is a PDF and renames it into
/// place, so a failed, bogus or oversized download never replaces (or
/// creates) `target`. Without a successful Commit the temporary file is
/// removed on destruction.
class PdfWriter {
  public:
    static constexpr std::size_t kDefaultMaxBytes = std::size_t{256} << 20;

    explicit PdfWriter(std::filesystem::path target, std::size_t max_bytes = kDefaultMaxBytes);
    ~PdfWriter();
    PdfWriter(const PdfWriter&) = delete;
    PdfWriter& operator=(const PdfWriter&) = delete;

    /// Append a chunk. False once the data cannot become a valid download
    /// (not a PDF, over the size limit, write error): stop the transfer.
    bool Write(std::string_view chunk);
    /// Finish: true if the complete file is a PDF and now sits at `target`.
    bool Commit();
    /// Why Write or Commit failed.
    const std::string& Error() const { return m_error; }

  private:
    std::filesystem::path m_target;
    std::filesystem::path m_temp;
    std::size_t m_max_bytes;
    std::size_t m_size = 0;
    std::string m_head; // first bytes, for the "%PDF-" check
    int m_fd = -1;
    bool m_failed = false;
    bool m_committed = false;
    std::string m_error;

    bool Fail(std::string why);
};

/// Keeps successive requests at least `interval` apart. arXiv's API terms
/// ask for no more than one request every three seconds. Thread-safe.
class RequestPacer {
  public:
    using Clock = std::chrono::steady_clock;
    using SleepFn = std::function<void(Clock::duration)>;
    using NowFn = std::function<Clock::time_point()>;

    explicit RequestPacer(
        Clock::duration interval,
        SleepFn sleep = [](Clock::duration d) { std::this_thread::sleep_for(d); },
        NowFn now = [] { return Clock::now(); });

    /// Block until the interval since the previous call has passed.
    void Wait();

  private:
    Clock::duration m_interval;
    SleepFn m_sleep;
    NowFn m_now;
    std::optional<Clock::time_point> m_last;
    std::mutex m_mutex;
};

class Fetcher {
  public:
    /// `http` defaults to the real (cpr) client; tests pass a scripted one.
    explicit Fetcher(const std::vector<std::string>& topics,
                     const std::string& base_path = "",
                     std::unique_ptr<HttpClient> http = nullptr);
    virtual ~Fetcher();

    /// Replace the pacing of arXiv API requests (tests record it instead of
    /// sleeping).
    void SetApiPacer(std::unique_ptr<RequestPacer> pacer) { m_api_pacer = std::move(pacer); }
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

    /// Longer than any real arXiv link (ids are at most ~30 characters). Longer
    /// input is never a paper, and is refused before any regex sees it:
    /// std::regex recurses per character and overflows the stack on ~100 KB.
    static constexpr std::size_t kMaxLinkLength = 256;

    /// Normalize an arXiv link to canonical form: https scheme, /abs/ (a /pdf/
    /// link and its ".pdf" are folded into it), no version suffix.
    /// e.g. "http://arxiv.org/abs/2605.28788v1" → "https://arxiv.org/abs/2605.28788"
    /// Links longer than kMaxLinkLength are returned unchanged.
    static std::string NormalizeLink(const std::string& link);

    /// True if `link` is an arXiv abstract or PDF URL
    /// ("https://arxiv.org/abs/<id>" or ".../pdf/<id>"), where <id> uses only
    /// the characters that appear in arXiv identifiers. Links reach file names
    /// and external programs, so anything else from a feed or an import is
    /// rejected.
    static bool IsArxivLink(const std::string& link);

    /// InspireHEP literature search for an arXiv id, with the id
    /// percent-encoded; nullopt if `paper_id` is not an arXiv identifier.
    static std::optional<std::string> InspireSearchUrl(const std::string& paper_id);

    /// True if `url` is an https URL on inspirehep.net — the only place the
    /// BibTeX link from a search response is allowed to point.
    static bool IsInspireUrl(const std::string& url);

    /// "arxiv-tui/<version> (+<project URL>)": sent with every request so
    /// arXiv and InspireHEP can tell who is calling.
    static std::string UserAgent();

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
    // FetchSince pages through export.arxiv.org; its terms of use ask for
    // at most one request every three seconds.
    std::unique_ptr<RequestPacer> m_api_pacer =
        std::make_unique<RequestPacer>(std::chrono::seconds(3));
    std::unique_ptr<HttpClient> m_http;
};

} // namespace Arxiv

#endif
