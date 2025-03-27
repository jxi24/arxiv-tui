#include "Arxiv/Fetcher.hh"
#include "Arxiv/Article.hh"

#include "cpr/cpr.h"
#include "spdlog/spdlog.h"
#include "fmt/ranges.h"
#include "pugixml.hpp"
#include <sstream>
#include <fstream>

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

std::vector<Article> Fetcher::FetchToday() {
    // For now, just return the most recent articles from Fetch()
    // In a real implementation, this would filter for today's articles
    return Fetch();
}

bool Fetcher::DownloadPaper(const std::string &paper_id, const std::string &output_path) {
    if(testing) {
        // In testing mode, just create an empty file
        std::ofstream file(output_path);
        return file.good();
    }

    try {
        auto url = ConstructPaperUrl(paper_id, "pdf");
        auto response = cpr::Get(cpr::Url{url});

        if(response.status_code == 200) {
            std::ofstream file(output_path, std::ios::binary);
            if (file.is_open()) {
                file.write(response.text.c_str(), static_cast<std::streamsize>(response.text.size()));
                file.close();
                spdlog::info("[Fetcher]: Successfully downloaded paper {} to {}", paper_id, output_path);
                return true;
            } else {
                spdlog::error("[Fetcher]: Failed to open output file {}", output_path);
                return false;
            }
        } else {
            spdlog::error("[Fetcher]: Failed to download paper {}: HTTP {}", paper_id, response.status_code);
            return false;
        }
    } catch (const std::exception &e) {
        spdlog::error("[Fetcher]: Error downloading paper {}: {}", paper_id, e.what());
        return false;
    }
}

std::string Fetcher::GetPaperAbstract(const std::string &paper_id) {
    if(testing) {
        return "Sample abstract for testing purposes.";
    }

    try {
        auto url = ConstructPaperUrl(paper_id, "abs");
        auto response = cpr::Get(cpr::Url{url});

        if(response.status_code == 200) {
            pugi::xml_document doc;
            if(doc.load_string(response.text.c_str())) {
                auto abstract_node = doc.select_node("//div[@class='abstract']");
                if(abstract_node) {
                    return abstract_node.node().text().get();
                }
            }
        }
        spdlog::error("[Fetcher]: Failed to fetch abstract for paper {}: HTTP {}", paper_id, response.status_code);
        return "";
    } catch (const std::exception &e) {
        spdlog::error("[Fetcher]: Error fetching abstract for paper {}: {}", paper_id, e.what());
        return "";
    }
}

std::string Fetcher::ConstructPaperUrl(const std::string &paper_id, const std::string &format) const {
    return fmt::format("https://arxiv.org/{}/{}", format, paper_id);
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
