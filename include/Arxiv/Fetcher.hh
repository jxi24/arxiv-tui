// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARXIV_FETCHER
#define ARXIV_FETCHER

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Arxiv {

using time_point = std::chrono::system_clock::time_point;

class Article;

class Fetcher {
  public:
    explicit Fetcher(const std::vector<std::string>& topics,
                     const std::string& base_path = "downloads");
    virtual ~Fetcher() = default;
    virtual std::vector<Article> Fetch();
    virtual std::vector<Article> FetchToday();
    /// Fetch all articles submitted since utc_date ("YYYY-MM-DD", inclusive
    /// of the day after utc_date) using the arXiv search API.
    virtual std::vector<Article> FetchSince(const std::string& utc_date);
    virtual bool DownloadPaper(const std::string& paper_id, const std::string& output_path);
    virtual std::string GetPaperAbstract(const std::string& paper_id);
    /// Fetch BibTeX for an arXiv paper. Tries InspireHEP first; returns an
    /// empty string if the lookup fails (caller should generate fallback BibTeX).
    virtual std::string FetchBibTeX(const std::string& paper_id);

  private:
    static constexpr bool testing = false;
    std::vector<std::string> m_topics;

    std::optional<std::string> FetchFeeds();
    std::vector<Article> ParseFeed(const std::string& content);
    std::vector<Article> ParseAtomFeed(const std::string& content);
    std::optional<time_point> ParseDate(const std::string& date) const;
    std::optional<time_point> ParseAtomDate(const std::string& date) const;
    std::string ConstructPaperUrl(const std::string& paper_id, const std::string& format) const;
    std::string ReplaceLatexAccents(const std::string& text) const;
    std::string StyleLatex(const std::string& text) const;
    std::filesystem::path base_path;
};

} // namespace Arxiv

#endif
