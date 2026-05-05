#include "Arxiv/Fetcher.hh"
#include "Arxiv/Article.hh"

#include "cpr/cpr.h"
#include "spdlog/spdlog.h"
#include "fmt/ranges.h"
#include "pugixml.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

using Arxiv::Fetcher;
using Arxiv::Article;

namespace {

// arXiv API endpoint used by FetchSince — pulled out as a constant so a
// future move (mirror, version pin) is one edit, not a string-search.
constexpr std::string_view ARXIV_API_URL = "https://export.arxiv.org/api/query";

// arXiv submittedDate query format is YYYYMMDDHHMI.
constexpr std::string_view ARXIV_QUERY_FROM_FORMAT = "%Y%m%d0000";
constexpr std::string_view ARXIV_QUERY_TO_FORMAT   = "%Y%m%d2359";

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
    if (date.size() < 10) return false;
    try {
        out.tm_year = std::stoi(date.substr(0, 4)) - 1900;
        out.tm_mon  = std::stoi(date.substr(5, 2)) - 1;
        out.tm_mday = std::stoi(date.substr(8, 2));
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace

Fetcher::Fetcher(const std::vector<std::string> &topics, const std::string &_base_path) : m_topics{topics} {
    base_path = _base_path;
    if(!std::filesystem::exists(base_path)) {
        std::filesystem::create_directory(base_path);
    } else if(!std::filesystem::is_directory(base_path)) {
        throw std::logic_error(fmt::format("[Fetcher]: {} already exists and is not a directory!",
                                           base_path.string()));
    }
}

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
            std::ofstream file(base_path / output_path, std::ios::binary);
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
            article.title = StyleLatex(ReplaceLatexAccents(item.child_value("title")));
            article.link = item.child_value("link");
            std::string abstract_text = item.child_value("description");
            // Find the position of "Abstract:" and remove everything up to and including it
            size_t abstract_pos = abstract_text.find("Abstract:");
            if (abstract_pos != std::string::npos) {
                abstract_text = abstract_text.substr(abstract_pos + 10); // 10 is length of "Abstract: "
            }
            article.abstract = StyleLatex(ReplaceLatexAccents(abstract_text));
            
            // Parse date
            auto date_str = item.child_value("pubDate");
            article.date = ParseDate(date_str).value_or(std::chrono::system_clock::now());

            // Extract authors (dc.creator)
            article.authors = StyleLatex(ReplaceLatexAccents(item.child("dc:creator").text().get()));

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

std::string Fetcher::StyleLatex(const std::string& text) const {
    std::string result = text;

    // Text formatting commands whose opening (\cmd{) we strip; the matching
    // closing brace is removed in the second pass below.
    const std::vector<std::pair<std::string, std::string>> replacements = {
        {"\\textit{", ""}, {"\\textbf{", ""}, {"\\emph{", ""},
        {"\\textsl{", ""}, {"\\textsc{", ""}, {"\\texttt{", ""},
        {"\\textnormal{", ""}, {"\\textrm{", ""}, {"\\textsf{", ""},
        {"\\textmd{", ""}, {"\\textup{", ""}, {"\\textdown{", ""},
        {"\\underline{", ""}, {"\\overline{", ""}, {"\\st{", ""}
    };

    apply_replacements(result, replacements);

    // Second pass: remove closing braces that follow formatting commands
    size_t pos = 0;
    while ((pos = result.find("}", pos)) != std::string::npos) {
        // Check if this closing brace is preceded by a formatting command
        bool is_formatting_brace = false;
        for (const auto& [latex, _] : replacements) {
            // Look back for the corresponding opening command
            size_t cmd_pos = result.rfind(latex, pos);
            if (cmd_pos != std::string::npos) {
                // Check if there's no other closing brace between the command and this one
                size_t next_brace = result.find("}", cmd_pos + 1);
                if (next_brace == pos) {
                    is_formatting_brace = true;
                    break;
                }
            }
        }
        
        if (is_formatting_brace) {
            result.erase(pos, 1);
        } else {
            pos++;
        }
    }

    return result;
}

std::string Fetcher::ReplaceLatexAccents(const std::string& text) const {
    std::string result = text;
    
    // Accent replacements
    const std::vector<std::pair<std::string, std::string>> replacements = {
        // Acute accent
        {"\\'a", "á"}, {"\\'e", "é"}, {"\\'i", "í"}, {"\\'o", "ó"}, {"\\'u", "ú"},
        {"\\'y", "ý"}, {"\\'A", "Á"}, {"\\'E", "É"}, {"\\'I", "Í"}, {"\\'O", "Ó"},
        {"\\'U", "Ú"}, {"\\'Y", "Ý"},
        
        // Grave accent
        {"\\`a", "à"}, {"\\`e", "è"}, {"\\`i", "ì"}, {"\\`o", "ò"}, {"\\`u", "ù"},
        {"\\`A", "À"}, {"\\`E", "È"}, {"\\`I", "Ì"}, {"\\`O", "Ò"}, {"\\`U", "Ù"},
        
        // Circumflex
        {"\\^a", "â"}, {"\\^e", "ê"}, {"\\^i", "î"}, {"\\^o", "ô"}, {"\\^u", "û"},
        {"\\^A", "Â"}, {"\\^E", "Ê"}, {"\\^I", "Î"}, {"\\^O", "Ô"}, {"\\^U", "Û"},
        
        // Tilde
        {"\\~a", "ã"}, {"\\~n", "ñ"}, {"\\~o", "õ"}, {"\\~A", "Ã"}, {"\\~N", "Ñ"},
        {"\\~O", "Õ"},
        
        // Umlaut/diaeresis
        {"\\\"a", "ä"}, {"\\\"e", "ë"}, {"\\\"i", "ï"}, {"\\\"o", "ö"}, {"\\\"u", "ü"},
        {"\\\"y", "ÿ"}, {"\\\"A", "Ä"}, {"\\\"E", "Ë"}, {"\\\"I", "Ï"}, {"\\\"O", "Ö"},
        {"\\\"U", "Ü"}, {"\\\"Y", "Ÿ"},
        
        // Ring
        {"\\r{a}", "å"}, {"\\r{A}", "Å"},
        
        // Cedilla
        {"\\c{c}", "ç"}, {"\\c{C}", "Ç"},
        
        // Caron (háček)
        {"\\v{a}", "ǎ"}, {"\\v{c}", "č"}, {"\\v{d}", "ď"}, {"\\v{e}", "ě"}, {"\\v{g}", "ğ"},
        {"\\v{h}", "ȟ"}, {"\\v{i}", "ǐ"}, {"\\v{j}", "ǰ"}, {"\\v{k}", "ǩ"}, {"\\v{l}", "ľ"},
        {"\\v{n}", "ň"}, {"\\v{o}", "ǒ"}, {"\\v{r}", "ř"}, {"\\v{s}", "š"}, {"\\v{t}", "ť"},
        {"\\v{u}", "ǔ"}, {"\\v{z}", "ž"},
        {"\\v{A}", "Ǎ"}, {"\\v{C}", "Č"}, {"\\v{D}", "Ď"}, {"\\v{E}", "Ě"}, {"\\v{G}", "Ğ"},
        {"\\v{H}", "Ȟ"}, {"\\v{I}", "Ǐ"}, {"\\v{K}", "Ǩ"}, {"\\v{L}", "Ľ"}, {"\\v{N}", "Ň"},
        {"\\v{O}", "Ǒ"}, {"\\v{R}", "Ř"}, {"\\v{S}", "Š"}, {"\\v{T}", "Ť"}, {"\\v{U}", "Ǔ"},
        {"\\v{Z}", "Ž"},
        
        // Dot above
        {"\\.a", "ȧ"}, {"\\.b", "ḃ"}, {"\\.c", "ċ"}, {"\\.d", "ḋ"}, {"\\.e", "ė"},
        {"\\.f", "ḟ"}, {"\\.g", "ġ"}, {"\\.h", "ḣ"}, {"\\.i", "ı"}, {"\\.m", "ṁ"},
        {"\\.n", "ṅ"}, {"\\.o", "ȯ"}, {"\\.p", "ṗ"}, {"\\.r", "ṙ"}, {"\\.s", "ṡ"},
        {"\\.t", "ṫ"}, {"\\.w", "ẇ"}, {"\\.x", "ẋ"}, {"\\.y", "ẏ"}, {"\\.z", "ż"},
        {"\\.A", "Ȧ"}, {"\\.B", "Ḃ"}, {"\\.C", "Ċ"}, {"\\.D", "Ḋ"}, {"\\.E", "Ė"},
        {"\\.F", "Ḟ"}, {"\\.G", "Ġ"}, {"\\.H", "Ḣ"}, {"\\.I", "İ"}, {"\\.M", "Ṁ"},
        {"\\.N", "Ṅ"}, {"\\.O", "Ȯ"}, {"\\.P", "Ṗ"}, {"\\.R", "Ṙ"}, {"\\.S", "Ṡ"},
        {"\\.T", "Ṫ"}, {"\\.W", "Ẇ"}, {"\\.X", "Ẋ"}, {"\\.Y", "Ẏ"}, {"\\.Z", "Ż"},
        
        // Special characters
        {"\\ss", "ß"}, {"\\SS", "ẞ"},
        {"\\ae", "æ"}, {"\\AE", "Æ"},
        {"\\oe", "œ"}, {"\\OE", "Œ"},
        {"\\o", "ø"}, {"\\O", "Ø"},
        {"\\l", "ł"}, {"\\L", "Ł"},
        {"\\i", "ı"}, {"\\j", "ȷ"},
        {"\\th", "þ"}, {"\\TH", "Þ"},
        {"\\dh", "ð"}, {"\\DH", "Ð"},
        {"\\ng", "ŋ"}, {"\\NG", "Ŋ"}
    };

    apply_replacements(result, replacements);
    return result;
}

std::vector<Article> Fetcher::FetchSince(const std::string &utc_date) {
    // Build date range: from utc_date up to today (inclusive), UTC.
    // The arXiv <published> field is the date arXiv processed and announced v1
    // of the paper (the posting date), per the arXiv API user manual:
    //   "<published> contains the date in which the first version of this
    //    article was submitted and processed."
    // This is NOT the peer-reviewed journal publication date (that is the
    // separate <arxiv:journal_ref> element).  Authors submit to arXiv one
    // business day before <published>; we start the query window at utc_date
    // itself so that papers whose <published> date equals utc_date are not
    // silently dropped.  Duplicates already in the DB are handled by
    // INSERT OR IGNORE in AddArticle.
    // arXiv submittedDate query format: YYYYMMDDHHMI (e.g. 202605020000).
    std::tm from_tm{};
    parse_ymd_prefix(utc_date, from_tm);
    timegm(&from_tm);  // normalise
    char from_buf[16];
    std::strftime(from_buf, sizeof(from_buf), ARXIV_QUERY_FROM_FORMAT.data(), &from_tm);

    // Today UTC as the end of the range.
    auto now = std::chrono::system_clock::now();
    std::time_t now_t = std::chrono::system_clock::to_time_t(now);
    std::tm to_tm{};
    gmtime_r(&now_t, &to_tm);
    char to_buf[16];
    std::strftime(to_buf, sizeof(to_buf), ARXIV_QUERY_TO_FORMAT.data(), &to_tm);

    if (std::string(from_buf) > std::string(to_buf)) {
        // utc_date is today or in the future — nothing missed.
        return {};
    }

    // Build topic query: (cat:hep-ph OR cat:hep-ex)
    std::string topic_query;
    for (size_t i = 0; i < m_topics.size(); ++i) {
        if (i > 0) topic_query += "+OR+";
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
        auto url = fmt::format(
            "{}?search_query={}{}&start={}&max_results={}"
            "&sortBy=submittedDate&sortOrder=descending",
            ARXIV_API_URL, topic_query, date_filter, start, max_results);

        spdlog::info("[Fetcher]: FetchSince GET {}", url);
        cpr::Response resp;
        try {
            resp = cpr::Get(cpr::Url{url}, cpr::Timeout{15000});
        } catch (const std::exception &e) {
            spdlog::error("[Fetcher]: FetchSince network error: {}", e.what());
            break;
        }

        if (resp.status_code != 200) {
            spdlog::warn("[Fetcher]: FetchSince HTTP {}", resp.status_code);
            break;
        }

        auto batch = ParseAtomFeed(resp.text);
        if (batch.empty()) break;
        all_articles.insert(all_articles.end(), batch.begin(), batch.end());
        if (static_cast<int>(batch.size()) < max_results) break;
        start += max_results;
    }

    spdlog::info("[Fetcher]: FetchSince got {} articles since {}", all_articles.size(), utc_date);
    return all_articles;
}

std::vector<Article> Fetcher::ParseAtomFeed(const std::string &xml_content) {
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

        // <id> holds the canonical URL, e.g. http://arxiv.org/abs/2605.12345v1
        article.link = entry.child_value("id");

        article.title    = StyleLatex(ReplaceLatexAccents(entry.child_value("title")));
        article.abstract = StyleLatex(ReplaceLatexAccents(entry.child_value("summary")));

        // Authors: one or more <author><name>…</name></author>
        std::string authors_str;
        for (auto author : entry.children("author")) {
            if (!authors_str.empty()) authors_str += ", ";
            authors_str += author.child_value("name");
        }
        article.authors = StyleLatex(ReplaceLatexAccents(authors_str));

        // <published> = arXiv processing/announcement date of v1 (the posting date).
        // Not the peer-reviewed journal date; that is <arxiv:journal_ref>.
        article.date = ParseAtomDate(entry.child_value("published"))
                           .value_or(std::chrono::system_clock::now());

        // Primary category: <arxiv:primary_category term="hep-ph" …/>
        // pugixml sees the element name as "arxiv:primary_category".
        auto prim_cat = entry.child("arxiv:primary_category");
        if (prim_cat) {
            article.category = prim_cat.attribute("term").value();
        } else {
            auto cat = entry.child("category");
            if (cat) article.category = cat.attribute("term").value();
        }

        articles.push_back(std::move(article));
    }

    return articles;
}

std::optional<Arxiv::time_point> Fetcher::ParseAtomDate(const std::string &date) const {
    // Format: "2026-05-04T00:00:00-04:00" or "2026-05-04T00:00:00Z"
    // We only need the date portion for day-level granularity.
    std::tm tm{};
    if (!parse_ymd_prefix(date, tm)) return std::nullopt;
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

std::string Fetcher::FetchBibTeX(const std::string& paper_id) {
    // --- 1. Try InspireHEP ---
    // Query the literature search API for the arXiv eprint.
    const std::string inspire_search =
        "https://inspirehep.net/api/literature?q=eprint+" + paper_id
        + "&fields=texkeys&size=1";
    try {
        auto search_resp = cpr::Get(
            cpr::Url{inspire_search},
            cpr::Timeout{5000});

        if (search_resp.status_code == 200) {
            auto js = nlohmann::json::parse(search_resp.text);
            auto& hits = js.at("hits").at("hits");
            if (!hits.empty()) {
                // The hit object carries a links.bibtex URL
                std::string bibtex_url =
                    hits[0].at("links").at("bibtex").get<std::string>();

                auto bib_resp = cpr::Get(
                    cpr::Url{bibtex_url},
                    cpr::Timeout{5000});
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
