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

std::string Fetcher::FetchBibtex(const std::string &arxiv_id) {
    try {
        auto url = fmt::format("https://inspirehep.net/api/arxiv/{}?format=bibtex", arxiv_id);
        auto response = cpr::Get(cpr::Url{url}, cpr::Timeout{5000});

        if(response.status_code == 200) {
            spdlog::info("[Fetcher]: Successfully fetched BibTeX for {}", arxiv_id);
            return response.text;
        } else {
            spdlog::warn("[Fetcher]: Failed to fetch BibTeX for {}: HTTP {}", arxiv_id, response.status_code);
            return "";
        }
    } catch (const std::exception &e) {
        spdlog::warn("[Fetcher]: Error fetching BibTeX for {}: {}", arxiv_id, e.what());
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
    
    // Text formatting
    const std::vector<std::pair<std::string, std::string>> replacements = {
        {"\\textit{", ""}, {"\\textbf{", ""}, {"\\emph{", ""},
        {"\\textsl{", ""}, {"\\textsc{", ""}, {"\\texttt{", ""},
        {"\\textnormal{", ""}, {"\\textrm{", ""}, {"\\textsf{", ""},
        {"\\textmd{", ""}, {"\\textup{", ""}, {"\\textdown{", ""},
        {"\\underline{", ""}, {"\\overline{", ""}, {"\\st{", ""}
    };

    // First pass: remove opening commands
    for (const auto& [latex, utf8] : replacements) {
        size_t pos = 0;
        while ((pos = result.find(latex, pos)) != std::string::npos) {
            result.replace(pos, latex.length(), utf8);
            pos += utf8.length();
        }
    }

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

    for (const auto& [latex, utf8] : replacements) {
        size_t pos = 0;
        while ((pos = result.find(latex, pos)) != std::string::npos) {
            result.replace(pos, latex.length(), utf8);
            pos += utf8.length();
        }
    }

    return result;
}
