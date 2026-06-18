// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Fetcher.hh"

#include "Arxiv/Article.hh"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "cpr/cpr.h"
#include "fmt/ranges.h"
#include "pugixml.hpp"
#include "spdlog/spdlog.h"

using Arxiv::Article;
using Arxiv::Fetcher;

namespace {

// arXiv API endpoint used by FetchSince — pulled out as a constant so a
// future move (mirror, version pin) is one edit, not a string-search.
constexpr std::string_view ARXIV_API_URL = "https://export.arxiv.org/api/query";

// arXiv submittedDate query format is YYYYMMDDHHMI.
constexpr std::string_view ARXIV_QUERY_FROM_FORMAT = "%Y%m%d0000";
constexpr std::string_view ARXIV_QUERY_TO_FORMAT = "%Y%m%d2359";

// Apply every (find, replace) entry in `table` to `text` in order, in-place.
// Both StyleLatex and ReplaceLatexAccents do this exact loop; isolating it
// removes the duplicate find-and-advance bookkeeping.
void apply_replacements(std::string& text,
                        const std::vector<std::pair<std::string, std::string>>& table) {
    for (const auto& [needle, sub] : table) {
        size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            text.replace(pos, needle.length(), sub);
            pos += sub.length();
        }
    }
}

// Parse the "YYYY-MM-DD" prefix of an ISO-8601 date string into tm fields.
// Returns false if the input is too short or non-numeric. Used by both the
// Atom-feed date parser and the FetchSince date-window builder.
bool parse_ymd_prefix(const std::string& date, std::tm& out) {
    if (date.size() < 10)
        return false;
    try {
        out.tm_year = std::stoi(date.substr(0, 4)) - 1900;
        out.tm_mon = std::stoi(date.substr(5, 2)) - 1;
        out.tm_mday = std::stoi(date.substr(8, 2));
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace

Fetcher::Fetcher(const std::vector<std::string>& topics, const std::string& _base_path)
    : m_topics{topics} {
    base_path = _base_path;
    if (!std::filesystem::exists(base_path)) {
        std::filesystem::create_directory(base_path);
    } else if (!std::filesystem::is_directory(base_path)) {
        throw std::logic_error(fmt::format("[Fetcher]: {} already exists and is not a directory!",
                                           base_path.string()));
    }
}

std::vector<Article> Fetcher::Fetch() {
    std::vector<Article> all_articles;
    auto response = FetchFeeds();
    if (response) {
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

bool Fetcher::DownloadPaper(const std::string& paper_id, const std::string& output_path) {
    if (testing) {
        // In testing mode, just create an empty file
        std::ofstream file(output_path);
        return file.good();
    }

    try {
        auto url = ConstructPaperUrl(paper_id, "pdf");
        auto response = cpr::Get(cpr::Url{url});

        if (response.status_code == 200) {
            std::ofstream file(base_path / output_path, std::ios::binary);
            if (file.is_open()) {
                file.write(response.text.c_str(),
                           static_cast<std::streamsize>(response.text.size()));
                file.close();
                spdlog::info(
                    "[Fetcher]: Successfully downloaded paper {} to {}", paper_id, output_path);
                return true;
            } else {
                spdlog::error("[Fetcher]: Failed to open output file {}", output_path);
                return false;
            }
        } else {
            spdlog::error(
                "[Fetcher]: Failed to download paper {}: HTTP {}", paper_id, response.status_code);
            return false;
        }
    } catch (const std::exception& e) {
        spdlog::error("[Fetcher]: Error downloading paper {}: {}", paper_id, e.what());
        return false;
    }
}

std::string Fetcher::GetPaperAbstract(const std::string& paper_id) {
    if (testing) {
        return "Sample abstract for testing purposes.";
    }

    try {
        auto url = ConstructPaperUrl(paper_id, "abs");
        auto response = cpr::Get(cpr::Url{url});

        if (response.status_code == 200) {
            pugi::xml_document doc;
            if (doc.load_string(response.text.c_str())) {
                auto abstract_node = doc.select_node("//div[@class='abstract']");
                if (abstract_node) {
                    return abstract_node.node().text().get();
                }
            }
        }
        spdlog::error("[Fetcher]: Failed to fetch abstract for paper {}: HTTP {}",
                      paper_id,
                      response.status_code);
        return "";
    } catch (const std::exception& e) {
        spdlog::error("[Fetcher]: Error fetching abstract for paper {}: {}", paper_id, e.what());
        return "";
    }
}

std::string Fetcher::NormalizeLink(const std::string& link) {
    std::string result = link;

    // Normalize scheme to https
    if (result.substr(0, 7) == "http://")
        result.replace(0, 7, "https://");

    // Strip trailing version suffix: "v" followed only by digits
    auto v_pos = result.rfind('v');
    if (v_pos != std::string::npos && v_pos + 1 < result.size()) {
        std::string_view suffix(result.c_str() + v_pos + 1);
        bool all_digits = !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit);
        if (all_digits)
            result.erase(v_pos);
    }

    return result;
}

std::string Fetcher::ConstructPaperUrl(const std::string& paper_id,
                                       const std::string& format) const {
    return fmt::format("https://arxiv.org/{}/{}", format, paper_id);
}

std::optional<std::string> Fetcher::FetchFeeds() {
    if (testing) {
        std::ifstream rss("hep-ph+hep-ex.rss");
        std::stringstream buffer;
        buffer << rss.rdbuf();
        return buffer.str();
    }
    spdlog::trace("[Fetcher]: Fetching articles for topics [{}]", fmt::join(m_topics, ", "));
    try {
        auto url = fmt::format("http://rss.arxiv.org/rss/{}", fmt::join(m_topics, "+"));

        auto response = cpr::Get(cpr::Url{url});

        if (response.status_code == 200) {
            return response.text;
        } else {
            spdlog::warn("[Fetcher]: Failed to fetch RSS");
            return std::nullopt;
        }

    } catch (const std::exception& e) {
        spdlog::error("[Fetcher]: Error fetching RSS {}", e.what());
        return std::nullopt;
    }
}

std::vector<Article> Fetcher::ParseFeed(const std::string& xml_content) const {
    std::vector<Article> articles;
    pugi::xml_document doc;

    // Parse XML content
    auto result = doc.load_string(xml_content.c_str());
    if (!result) {
        spdlog::error("[Fetcher]: XML parsing error {}", result.description());
        return articles;
    }

    try {
        // Navigate to channel
        auto channel = doc.select_node("//channel").node();
        if (!channel) {
            spdlog::error("[Fetcher]: Could not find channel node");
            return articles;
        }

        // Iterate through items
        for (auto item : channel.children("item")) {
            Article article;

            // Extract basic fields using node values
            article.title = LatexToMarkdown(item.child_value("title"));
            article.link = NormalizeLink(item.child_value("link"));
            std::string abstract_text = item.child_value("description");
            // Find the position of "Abstract:" and remove everything up to and including it
            size_t abstract_pos = abstract_text.find("Abstract:");
            if (abstract_pos != std::string::npos) {
                abstract_text =
                    abstract_text.substr(abstract_pos + 10); // 10 is length of "Abstract: "
            }
            article.abstract = LatexToMarkdown(abstract_text);

            // Parse date
            auto date_str = item.child_value("pubDate");
            article.date = ParseDate(date_str).value_or(std::chrono::system_clock::now());

            // Extract authors (dc.creator)
            article.authors = LatexToMarkdown(item.child("dc:creator").text().get());

            // Collect all categories
            std::stringstream categories;
            bool first = true;
            for (auto category : item.children("category")) {
                if (!first)
                    categories << ", ";
                categories << category.text().get();
                first = false;
            }
            article.category = categories.str();

            // Replacement detection. arxiv RSS marks replacements either via
            //   <dc:type>replace</dc:type>  / "Replacement"
            //   <arxiv:announce_type>replace…</arxiv:announce_type>
            // or by suffixing the title with " (UPDATED)". Be permissive.
            std::string dc_type = item.child_value("dc:type");
            std::string ann_type = item.child_value("arxiv:announce_type");
            auto contains_ci = [](const std::string& hay, const std::string& needle) {
                auto it = std::search(
                    hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
                        return std::tolower(static_cast<unsigned char>(a)) ==
                               std::tolower(static_cast<unsigned char>(b));
                    });
                return it != hay.end();
            };
            article.is_replacement = contains_ci(dc_type, "replace") ||
                                     contains_ci(ann_type, "replace") ||
                                     contains_ci(article.title, "(UPDATED)");

            articles.push_back(article);
        }
    } catch (const pugi::xpath_exception& e) {
        spdlog::error("[Fetcher] XPath error {}", e.what());
    } catch (const std::exception& e) {
        spdlog::error("[Fetcher] Error parsing RSS {}", e.what());
    }

    return articles;
}

std::optional<Arxiv::time_point> Fetcher::ParseDate(const std::string& date) const {
    std::istringstream ss(date);
    std::tm tm = {};
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

    if (ss.fail()) {
        return std::nullopt;
    }

    // Convert to time_point
    std::time_t time = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(time);
}

// Return the position of the closing brace that matches the '{' at open_pos,
// respecting nested braces.  Returns std::string::npos if unmatched.
static size_t find_closing_brace(const std::string& s, size_t open_pos) {
    int depth = 1;
    for (size_t i = open_pos + 1; i < s.size(); ++i) {
        if (s[i] == '{')
            ++depth;
        else if (s[i] == '}' && --depth == 0)
            return i;
    }
    return std::string::npos;
}

// Apply one pass of command→marker substitutions using proper brace matching.
// Each entry is { "\\cmd", { "open_marker", "close_marker" } }.
// An empty marker pair means "strip the command, keep the content."
static void apply_latex_conversions(
    std::string& result,
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>>& table) {
    for (const auto& [cmd, markers] : table) {
        const auto& [open_md, close_md] = markers;
        std::string open_tok = cmd + "{";
        size_t pos = 0;
        while ((pos = result.find(open_tok, pos)) != std::string::npos) {
            size_t brace = pos + cmd.size(); // position of '{'
            size_t close = find_closing_brace(result, brace);
            if (close == std::string::npos) {
                ++pos;
                continue;
            }
            std::string content = result.substr(brace + 1, close - brace - 1);
            std::string replacement = open_md + content + close_md;
            result.replace(pos, close - pos + 1, replacement);
            pos += replacement.size();
        }
    }
}

std::string Fetcher::LatexToMarkdown(const std::string& text) const {
    std::string result = ReplaceLatexAccents(text);

    // Commands with Markdown equivalents.
    static const std::vector<std::pair<std::string, std::pair<std::string, std::string>>>
        conversions = {
            {"\\textbf", {"**", "**"}},
            {"\\textit", {"*", "*"}},
            {"\\emph", {"*", "*"}},
            {"\\textsl", {"*", "*"}},
            {"\\texttt", {"`", "`"}},
            {"\\st", {"~~", "~~"}},
            // No Markdown equivalent — strip the command, keep the content.
            {"\\textsc", {"", ""}},
            {"\\textnormal", {"", ""}},
            {"\\textrm", {"", ""}},
            {"\\textsf", {"", ""}},
            {"\\textmd", {"", ""}},
            {"\\textup", {"", ""}},
            {"\\textdown", {"", ""}},
            {"\\underline", {"", ""}},
            {"\\overline", {"", ""}},
        };

    // Repeat until no further substitutions are made (handles nesting).
    std::string prev;
    do {
        prev = result;
        apply_latex_conversions(result, conversions);
    } while (result != prev);

    return result;
}

std::string Fetcher::StyleLatex(const std::string& text) const {
    std::string result = text;

    // For each formatting command, replace \cmd{content} with content in one
    // pass so both the opening marker and its matching closing brace are
    // removed together.  Processing innermost first (repeated until stable)
    // handles nested commands correctly.
    static const std::vector<std::string> commands = {"\\textit",
                                                      "\\textbf",
                                                      "\\emph",
                                                      "\\textsl",
                                                      "\\textsc",
                                                      "\\texttt",
                                                      "\\textnormal",
                                                      "\\textrm",
                                                      "\\textsf",
                                                      "\\textmd",
                                                      "\\textup",
                                                      "\\textdown",
                                                      "\\underline",
                                                      "\\overline",
                                                      "\\st"};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& cmd : commands) {
            std::string open = cmd + "{";
            size_t pos = 0;
            while ((pos = result.find(open, pos)) != std::string::npos) {
                size_t content_start = pos + open.size();
                size_t close = result.find("}", content_start);
                if (close == std::string::npos)
                    break;
                result.replace(
                    pos, close - pos + 1, result.substr(content_start, close - content_start));
                changed = true;
                // Re-scan from same position — the replacement may itself
                // contain a further instance of the same command.
            }
        }
    }

    return result;
}

std::string Fetcher::ReplaceLatexAccents(const std::string& text) const {
    std::string result = text;

    // Accent replacements
    const std::vector<std::pair<std::string, std::string>> replacements = {// Acute accent
                                                                           {"\\'a", "á"},
                                                                           {"\\'e", "é"},
                                                                           {"\\'i", "í"},
                                                                           {"\\'o", "ó"},
                                                                           {"\\'u", "ú"},
                                                                           {"\\'y", "ý"},
                                                                           {"\\'A", "Á"},
                                                                           {"\\'E", "É"},
                                                                           {"\\'I", "Í"},
                                                                           {"\\'O", "Ó"},
                                                                           {"\\'U", "Ú"},
                                                                           {"\\'Y", "Ý"},

                                                                           // Grave accent
                                                                           {"\\`a", "à"},
                                                                           {"\\`e", "è"},
                                                                           {"\\`i", "ì"},
                                                                           {"\\`o", "ò"},
                                                                           {"\\`u", "ù"},
                                                                           {"\\`A", "À"},
                                                                           {"\\`E", "È"},
                                                                           {"\\`I", "Ì"},
                                                                           {"\\`O", "Ò"},
                                                                           {"\\`U", "Ù"},

                                                                           // Circumflex
                                                                           {"\\^a", "â"},
                                                                           {"\\^e", "ê"},
                                                                           {"\\^i", "î"},
                                                                           {"\\^o", "ô"},
                                                                           {"\\^u", "û"},
                                                                           {"\\^A", "Â"},
                                                                           {"\\^E", "Ê"},
                                                                           {"\\^I", "Î"},
                                                                           {"\\^O", "Ô"},
                                                                           {"\\^U", "Û"},

                                                                           // Tilde
                                                                           {"\\~a", "ã"},
                                                                           {"\\~n", "ñ"},
                                                                           {"\\~o", "õ"},
                                                                           {"\\~A", "Ã"},
                                                                           {"\\~N", "Ñ"},
                                                                           {"\\~O", "Õ"},

                                                                           // Umlaut/diaeresis
                                                                           {"\\\"a", "ä"},
                                                                           {"\\\"e", "ë"},
                                                                           {"\\\"i", "ï"},
                                                                           {"\\\"o", "ö"},
                                                                           {"\\\"u", "ü"},
                                                                           {"\\\"y", "ÿ"},
                                                                           {"\\\"A", "Ä"},
                                                                           {"\\\"E", "Ë"},
                                                                           {"\\\"I", "Ï"},
                                                                           {"\\\"O", "Ö"},
                                                                           {"\\\"U", "Ü"},
                                                                           {"\\\"Y", "Ÿ"},

                                                                           // Ring
                                                                           {"\\r{a}", "å"},
                                                                           {"\\r{A}", "Å"},

                                                                           // Cedilla
                                                                           {"\\c{c}", "ç"},
                                                                           {"\\c{C}", "Ç"},

                                                                           // Caron (háček)
                                                                           {"\\v{a}", "ǎ"},
                                                                           {"\\v{c}", "č"},
                                                                           {"\\v{d}", "ď"},
                                                                           {"\\v{e}", "ě"},
                                                                           {"\\v{g}", "ğ"},
                                                                           {"\\v{h}", "ȟ"},
                                                                           {"\\v{i}", "ǐ"},
                                                                           {"\\v{j}", "ǰ"},
                                                                           {"\\v{k}", "ǩ"},
                                                                           {"\\v{l}", "ľ"},
                                                                           {"\\v{n}", "ň"},
                                                                           {"\\v{o}", "ǒ"},
                                                                           {"\\v{r}", "ř"},
                                                                           {"\\v{s}", "š"},
                                                                           {"\\v{t}", "ť"},
                                                                           {"\\v{u}", "ǔ"},
                                                                           {"\\v{z}", "ž"},
                                                                           {"\\v{A}", "Ǎ"},
                                                                           {"\\v{C}", "Č"},
                                                                           {"\\v{D}", "Ď"},
                                                                           {"\\v{E}", "Ě"},
                                                                           {"\\v{G}", "Ğ"},
                                                                           {"\\v{H}", "Ȟ"},
                                                                           {"\\v{I}", "Ǐ"},
                                                                           {"\\v{K}", "Ǩ"},
                                                                           {"\\v{L}", "Ľ"},
                                                                           {"\\v{N}", "Ň"},
                                                                           {"\\v{O}", "Ǒ"},
                                                                           {"\\v{R}", "Ř"},
                                                                           {"\\v{S}", "Š"},
                                                                           {"\\v{T}", "Ť"},
                                                                           {"\\v{U}", "Ǔ"},
                                                                           {"\\v{Z}", "Ž"},

                                                                           // Dot above
                                                                           {"\\.a", "ȧ"},
                                                                           {"\\.b", "ḃ"},
                                                                           {"\\.c", "ċ"},
                                                                           {"\\.d", "ḋ"},
                                                                           {"\\.e", "ė"},
                                                                           {"\\.f", "ḟ"},
                                                                           {"\\.g", "ġ"},
                                                                           {"\\.h", "ḣ"},
                                                                           {"\\.i", "ı"},
                                                                           {"\\.m", "ṁ"},
                                                                           {"\\.n", "ṅ"},
                                                                           {"\\.o", "ȯ"},
                                                                           {"\\.p", "ṗ"},
                                                                           {"\\.r", "ṙ"},
                                                                           {"\\.s", "ṡ"},
                                                                           {"\\.t", "ṫ"},
                                                                           {"\\.w", "ẇ"},
                                                                           {"\\.x", "ẋ"},
                                                                           {"\\.y", "ẏ"},
                                                                           {"\\.z", "ż"},
                                                                           {"\\.A", "Ȧ"},
                                                                           {"\\.B", "Ḃ"},
                                                                           {"\\.C", "Ċ"},
                                                                           {"\\.D", "Ḋ"},
                                                                           {"\\.E", "Ė"},
                                                                           {"\\.F", "Ḟ"},
                                                                           {"\\.G", "Ġ"},
                                                                           {"\\.H", "Ḣ"},
                                                                           {"\\.I", "İ"},
                                                                           {"\\.M", "Ṁ"},
                                                                           {"\\.N", "Ṅ"},
                                                                           {"\\.O", "Ȯ"},
                                                                           {"\\.P", "Ṗ"},
                                                                           {"\\.R", "Ṙ"},
                                                                           {"\\.S", "Ṡ"},
                                                                           {"\\.T", "Ṫ"},
                                                                           {"\\.W", "Ẇ"},
                                                                           {"\\.X", "Ẋ"},
                                                                           {"\\.Y", "Ẏ"},
                                                                           {"\\.Z", "Ż"},

                                                                           // Special characters
                                                                           {"\\ss", "ß"},
                                                                           {"\\SS", "ẞ"},
                                                                           {"\\ae", "æ"},
                                                                           {"\\AE", "Æ"},
                                                                           {"\\oe", "œ"},
                                                                           {"\\OE", "Œ"},
                                                                           {"\\o", "ø"},
                                                                           {"\\O", "Ø"},
                                                                           {"\\l", "ł"},
                                                                           {"\\L", "Ł"},
                                                                           {"\\i", "ı"},
                                                                           {"\\j", "ȷ"},
                                                                           {"\\th", "þ"},
                                                                           {"\\TH", "Þ"},
                                                                           {"\\dh", "ð"},
                                                                           {"\\DH", "Ð"},
                                                                           {"\\ng", "ŋ"},
                                                                           {"\\NG", "Ŋ"}};

    apply_replacements(result, replacements);
    return result;
}

std::vector<Article> Fetcher::FetchSince(const std::string& utc_date) {
    // Build date range: from utc_date up to today (inclusive), UTC.
    // We query and stamp by the arXiv submission date. Per the arXiv API user
    // manual the <published> element holds it:
    //   "<published> contains the date in which the first version of this
    //    article was submitted and processed."
    // This is NOT the peer-reviewed journal publication date (that is the
    // separate <arxiv:journal_ref> element).  We start the query window at
    // utc_date itself so that papers whose submission date equals utc_date are
    // not silently dropped.  Duplicates already in the DB are handled by
    // INSERT OR IGNORE in AddArticle.
    // arXiv submittedDate query format: YYYYMMDDHHMI (e.g. 202605020000).
    std::tm from_tm{};
    parse_ymd_prefix(utc_date, from_tm);
    timegm(&from_tm); // normalise
    char from_buf[16];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    std::strftime(from_buf, sizeof(from_buf), ARXIV_QUERY_FROM_FORMAT.data(), &from_tm);
#pragma GCC diagnostic pop

    // Today UTC as the end of the range.
    auto now = std::chrono::system_clock::now();
    std::time_t now_t = std::chrono::system_clock::to_time_t(now);
    std::tm to_tm{};
    gmtime_r(&now_t, &to_tm);
    char to_buf[16];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    std::strftime(to_buf, sizeof(to_buf), ARXIV_QUERY_TO_FORMAT.data(), &to_tm);
#pragma GCC diagnostic pop

    if (std::string(from_buf) > std::string(to_buf)) {
        // utc_date is today or in the future — nothing missed.
        return {};
    }

    // Build topic query: (cat:hep-ph OR cat:hep-ex)
    std::string topic_query;
    for (size_t i = 0; i < m_topics.size(); ++i) {
        if (i > 0)
            topic_query += "+OR+";
        topic_query += "cat:" + m_topics[i];
    }
    if (m_topics.size() > 1)
        topic_query = "(" + topic_query + ")";

    const std::string date_filter =
        "+AND+submittedDate:[" + std::string(from_buf) + "+TO+" + std::string(to_buf) + "]";

    std::vector<Article> all_articles;
    int start = 0;
    const int max_results = 200;

    while (true) {
        auto url = fmt::format("{}?search_query={}{}&start={}&max_results={}"
                               "&sortBy=submittedDate&sortOrder=descending",
                               ARXIV_API_URL,
                               topic_query,
                               date_filter,
                               start,
                               max_results);

        spdlog::info("[Fetcher]: FetchSince GET {}", url);
        cpr::Response resp;
        try {
            resp = cpr::Get(cpr::Url{url}, cpr::Timeout{15000});
        } catch (const std::exception& e) {
            spdlog::error("[Fetcher]: FetchSince network error: {}", e.what());
            break;
        }

        if (resp.status_code != 200) {
            spdlog::warn("[Fetcher]: FetchSince HTTP {}", resp.status_code);
            break;
        }

        auto batch = ParseAtomFeed(resp.text);
        if (batch.empty())
            break;
        all_articles.insert(all_articles.end(), batch.begin(), batch.end());
        if (static_cast<int>(batch.size()) < max_results)
            break;
        start += max_results;
    }

    // Fold in today's freshly-announced papers from the RSS feed. The API's
    // submittedDate window lags the announcement — papers are listed a day or
    // two after submission — so today's new articles would otherwise not show
    // up until a later open. Appended last so today's announcement wins on any
    // overlap when the DB de-dupes by link. This lets a single FetchSince call
    // cover both backfill and today, so callers need no separate Fetch().
    auto todays = Fetch();
    all_articles.insert(all_articles.end(),
                        std::make_move_iterator(todays.begin()),
                        std::make_move_iterator(todays.end()));

    spdlog::info("[Fetcher]: FetchSince got {} articles since {}", all_articles.size(), utc_date);
    return all_articles;
}

std::vector<Article> Fetcher::ParseAtomFeed(const std::string& xml_content) const {
    std::vector<Article> articles;
    pugi::xml_document doc;

    auto result = doc.load_string(xml_content.c_str());
    if (!result) {
        spdlog::error("[Fetcher]: Atom XML parse error: {}", result.description());
        return articles;
    }

    // Root element is <feed xmlns="http://www.w3.org/2005/Atom">.
    // pugixml treats element names as raw strings (no namespace processing),
    // so elements in the default Atom namespace appear with their local names.
    auto feed = doc.child("feed");
    if (!feed) {
        spdlog::error("[Fetcher]: ParseAtomFeed: no <feed> root");
        return articles;
    }

    for (auto entry : feed.children("entry")) {
        Article article;

        // <id> holds the URL e.g. http://arxiv.org/abs/2605.12345v1; normalize it.
        std::string raw_link = entry.child_value("id");
        article.link = NormalizeLink(raw_link);

        article.title = LatexToMarkdown(entry.child_value("title"));
        article.abstract = LatexToMarkdown(entry.child_value("summary"));

        // Authors: one or more <author><name>…</name></author>
        std::string authors_str;
        for (auto author : entry.children("author")) {
            if (!authors_str.empty())
                authors_str += ", ";
            authors_str += author.child_value("name");
        }
        article.authors = LatexToMarkdown(authors_str);

        // <published> = the date v1 was submitted and processed — the same
        // submission date the FetchSince query filters on. Not the
        // peer-reviewed journal date; that is <arxiv:journal_ref>.
        article.date = ParseAtomDate(entry.child_value("published"))
                           .value_or(std::chrono::system_clock::now());

        // Primary category: <arxiv:primary_category term="hep-ph" …/>
        // pugixml sees the element name as "arxiv:primary_category".
        auto prim_cat = entry.child("arxiv:primary_category");
        if (prim_cat) {
            article.category = prim_cat.attribute("term").value();
        } else {
            auto cat = entry.child("category");
            if (cat)
                article.category = cat.attribute("term").value();
        }

        // Replacement detection. arxiv's atom output uses
        //   <arxiv:announce_type>replace</arxiv:announce_type>     (and
        //   "replace-cross"). Treat the substring "replace" as the signal.
        // As a fallback the <id> is "<base>v<N>" — anything past v1 is a
        // replacement when announce_type is missing.
        std::string ann = entry.child_value("arxiv:announce_type");
        if (ann.find("replace") != std::string::npos) {
            article.is_replacement = true;
        } else if (ann.empty()) {
            auto v_pos = raw_link.rfind('v');
            if (v_pos != std::string::npos && v_pos + 1 < raw_link.size()) {
                std::string ver = raw_link.substr(v_pos + 1);
                bool all_digits = !ver.empty() && std::all_of(ver.begin(), ver.end(), [](char c) {
                    return std::isdigit(c);
                });
                if (all_digits && ver != "1")
                    article.is_replacement = true;
            }
        }

        articles.push_back(std::move(article));
    }

    return articles;
}

std::optional<Arxiv::time_point> Fetcher::ParseAtomDate(const std::string& date) const {
    // Format: "2026-05-04T00:00:00-04:00" or "2026-05-04T00:00:00Z"
    // We only need the date portion for day-level granularity.
    std::tm tm{};
    if (!parse_ymd_prefix(date, tm))
        return std::nullopt;
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

std::string Fetcher::FetchBibTeX(const std::string& paper_id) {
    // --- 1. Try InspireHEP ---
    // Query the literature search API for the arXiv eprint.
    const std::string inspire_search =
        "https://inspirehep.net/api/literature?q=eprint+" + paper_id + "&fields=texkeys&size=1";
    try {
        auto search_resp = cpr::Get(cpr::Url{inspire_search}, cpr::Timeout{5000});

        if (search_resp.status_code == 200) {
            auto js = nlohmann::json::parse(search_resp.text);
            auto& hits = js.at("hits").at("hits");
            if (!hits.empty()) {
                // The hit object carries a links.bibtex URL
                std::string bibtex_url = hits[0].at("links").at("bibtex").get<std::string>();

                auto bib_resp = cpr::Get(cpr::Url{bibtex_url}, cpr::Timeout{5000});
                if (bib_resp.status_code == 200 && !bib_resp.text.empty()) {
                    spdlog::info("[Fetcher]: Got InspireHEP BibTeX for {}", paper_id);
                    return bib_resp.text;
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("[Fetcher]: InspireHEP lookup failed for {}: {}", paper_id, e.what());
    }

    // --- 2. arXiv fallback ---
    // Return empty; AppCore will construct BibTeX from its Article struct.
    spdlog::debug("[Fetcher]: Returning empty BibTeX for {} (no InspireHEP hit)", paper_id);
    return {};
}
