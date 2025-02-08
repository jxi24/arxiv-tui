#ifndef ARXIV_FETCHER
#define ARXIV_FETCHER

#include <chrono>
#include <optional>
#include <string>
#include <vector>


namespace Arxiv {

using time_point = std::chrono::system_clock::time_point;

struct Article {
    std::string title;
    std::string link;
    std::string abstract;
    std::string authors;
    time_point date;
    std::string category;
    bool bookmarked{false};

    bool operator<(const Article &other) const {
        return date < other.date;
    }
};

class Fetcher {
  public:
    explicit Fetcher(const std::vector<std::string> &topics);
    std::vector<Article> Fetch();
    std::vector<Article> FetchToday();

  private:
    static constexpr bool testing = true;
    std::vector<std::string> m_topics; 

    std::optional<std::string> FetchFeeds();
    std::vector<Article> ParseFeed(const std::string &content);
    std::optional<time_point> ParseDate(const std::string &date) const;
};

}

#endif
