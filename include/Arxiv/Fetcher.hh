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
    explicit Fetcher(const std::vector<std::string> &topics, const std::string &base_path="downloads");
    virtual ~Fetcher() = default;
    virtual std::vector<Article> Fetch();
    virtual std::vector<Article> FetchToday();
    virtual bool DownloadPaper(const std::string &paper_id, const std::string &output_path);
    virtual std::string GetPaperAbstract(const std::string &paper_id);

  private:
    static constexpr bool testing = false;
    std::vector<std::string> m_topics; 

    std::optional<std::string> FetchFeeds();
    std::vector<Article> ParseFeed(const std::string &content);
    std::optional<time_point> ParseDate(const std::string &date) const;
    std::string ConstructPaperUrl(const std::string &paper_id, const std::string &format) const;
    std::filesystem::path base_path;
};

}

#endif
