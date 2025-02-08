#include "Arxiv/Fetcher.hh"

#include "cpr/cpr.h"
#include "spdlog/spdlog.h"
#include "fmt/ranges.h"
#include "pugixml.hpp"
#include <sstream>

using Arxiv::Fetcher;
using Arxiv::Article;

Fetcher::Fetcher(const std::vector<std::string> &topics) : m_topics{topics} {}

std::vector<Article> Fetcher::Fetch() {
    std::vector<Article> all_articles;
    auto response = FetchFeeds();
    if(response) {
        all_articles = ParseFeed(response.value());
    }

    spdlog::info("[Fetcher]: Fetched {} articles", all_articles.size());
    return all_articles;
}

std::optional<std::string> Fetcher::FetchFeeds() {
    if(testing) {
        std::ifstream rss("hep-ph+hep-ex.rss");
        std::stringstream buffer;
        buffer << rss.rdbuf();
        return buffer.str();
    }
    spdlog::trace("[Fetcher]: Fetching articles for topics [{}]",
                  fmt::join(m_topics, ", "));
    try {
        auto url = fmt::format("http://rss.arxiv.org/rss/{}",
                               fmt::join(m_topics, "+"));

        auto response = cpr::Get(cpr::Url{url});

        if(response.status_code == 200) {
            return response.text;
        } else {
            spdlog::warn("[Fetcher]: Failed to fetch RSS");
            return std::nullopt;
        }

    } catch (const std::exception &e) {
        spdlog::error("[Fetcher]: Error fetching RSS {}", e.what());
        return std::nullopt;
    }
}

std::vector<Article> Fetcher::ParseFeed(const std::string &xml_content) {
    std::vector<Article> articles;
    pugi::xml_document doc;

    // Parse XML content
    auto result = doc.load_string(xml_content.c_str());
    if(!result) {
        spdlog::error("[Fetcher]: XML parsing error {}", result.description());
        return articles;
    }

    try {
        // Navigate to channel
        auto channel = doc.select_node("//channel").node();
        if(!channel) {
            spdlog::error("[Fetcher]: Could not find channel node");
            return articles;
        }

        // Iterate through items
        for(auto item : channel.children("item")) {
            Article article;

            // Extract basic fields using node values
            article.title = item.child_value("title");
            article.link = item.child_value("link");
            article.abstract = item.child_value("description");
            
            // Parse date
            auto date_str = item.child_value("pubDate");
            article.date = ParseDate(date_str).value_or(std::chrono::system_clock::now());

            // Extract authors (dc.creator)
            article.authors = item.child("dc:creator").text().get();

            // Collect all categories
            std::stringstream categories;
            bool first = true;
            for(auto category : item.children("category")) {
                if(!first) categories << ", ";
                categories << category.text().get();
                first = false;
            }
            article.category = categories.str();

            articles.push_back(article);
        }
    } catch (const pugi::xpath_exception &e) {
        spdlog::error("[Fetcher] XPath error {}", e.what());
    } catch (const std::exception &e) {
        spdlog::error("[Fetcher] Error parsing RSS {}", e.what());
    }

    return articles;
}

std::optional<Arxiv::time_point> Fetcher::ParseDate(const std::string &date) const {
    std::istringstream ss(date);
    std::tm tm = {};
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

    if(ss.fail()) {
        return std::nullopt;
    }

    // Convert to time_point
    std::time_t time = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(time);
}
