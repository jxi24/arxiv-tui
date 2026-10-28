// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Fetcher.hh"

#include "Arxiv/Article.hh"
#include "Arxiv/HttpClient.hh"
#include "Arxiv/Logging.hh"
#include "Arxiv/Version.hh"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <regex>
#include <sstream>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

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
// Used by ReplaceLatexAccents.
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

Fetcher::Fetcher(const std::vector<std::string>& topics,
                 const std::string& _base_path,
                 std::unique_ptr<HttpClient> http)
    : m_topics{topics}
    , m_http(http ? std::move(http) : MakeCprHttpClient()) {
    base_path = _base_path;
    // Without a download directory, DownloadPaper needs absolute paths.
    if (base_path.empty())
        return;
    if (!std::filesystem::exists(base_path)) {
        std::filesystem::create_directory(base_path);
    } else if (!std::filesystem::is_directory(base_path)) {
        throw std::logic_error(fmt::format("[Fetcher]: {} already exists and is not a directory!",
                                           base_path.string()));
    }
}

std::vector<Article> Fetcher::Fetch() {
    auto response = FetchFeeds();
    if (!response)
        throw FetchError("arXiv RSS feed could not be fetched");
    std::vector<Article> all_articles = ParseFeed(response.value());

    spdlog::info("[Fetcher]: Fetched {} articles", all_articles.size());
    return all_articles;
}

// ---------------------------------------------------------------------------
// PdfWriter
// ---------------------------------------------------------------------------

namespace {
constexpr std::string_view kPdfMagic = "%PDF-";
}

Arxiv::PdfWriter::PdfWriter(std::filesystem::path target, std::size_t max_bytes)
    : m_target(std::move(target))
    , m_max_bytes(max_bytes) {
    std::string temp = m_target.string() + ".part-XXXXXX";
    m_fd = ::mkostemp(temp.data(), O_CLOEXEC);
    if (m_fd < 0)
        Fail(fmt::format("cannot create a temporary file beside {}: {}",
                         m_target.string(),
                         std::strerror(errno)));
    else
        m_temp = temp;
}

Arxiv::PdfWriter::~PdfWriter() {
    if (m_fd >= 0)
        ::close(m_fd);
    if (!m_committed && !m_temp.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_temp, ec);
    }
}

bool Arxiv::PdfWriter::Fail(std::string why) {
    m_failed = true;
    if (m_error.empty())
        m_error = std::move(why);
    return false;
}

bool Arxiv::PdfWriter::Write(std::string_view chunk) {
    if (m_failed)
        return false;
    if (chunk.size() > m_max_bytes - m_size)
        return Fail(fmt::format("larger than {} bytes", m_max_bytes));
    if (m_head.size() < kPdfMagic.size()) {
        m_head.append(chunk.substr(0, kPdfMagic.size() - m_head.size()));
        if (kPdfMagic.compare(0, m_head.size(), m_head) != 0)
            return Fail("the server did not send a PDF");
    }
    while (!chunk.empty()) {
        ssize_t n = ::write(m_fd, chunk.data(), chunk.size());
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return Fail(fmt::format("write failed: {}", std::strerror(errno)));
        chunk.remove_prefix(static_cast<std::size_t>(n));
        m_size += static_cast<std::size_t>(n);
    }
    return true;
}

bool Arxiv::PdfWriter::Commit() {
    if (m_failed)
        return false;
    if (m_head != kPdfMagic)
        return Fail("the server did not send a PDF");
    // Data on disk before the rename, so a crash leaves the old file or the
    // whole new one.
    if (::fsync(m_fd) != 0)
        return Fail(fmt::format("write failed: {}", std::strerror(errno)));
    ::close(m_fd);
    m_fd = -1;
    std::error_code ec;
    std::filesystem::rename(m_temp, m_target, ec);
    if (ec)
        return Fail(fmt::format("cannot move the download into place: {}", ec.message()));
    m_committed = true;
    return true;
}

Arxiv::RequestPacer::RequestPacer(Clock::duration interval, SleepFn sleep, NowFn now)
    : m_interval(interval)
    , m_sleep(std::move(sleep))
    , m_now(std::move(now)) {}

void Arxiv::RequestPacer::Wait() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_last) {
        auto elapsed = m_now() - *m_last;
        if (elapsed < m_interval)
            m_sleep(m_interval - elapsed);
    }
    m_last = m_now();
}

Fetcher::~Fetcher() = default;

std::string Fetcher::UserAgent() {
    return fmt::format("arxiv-tui/{} (+https://github.com/jxi24/arxiv-tui)", kVersion);
}

bool Fetcher::DownloadPaper(const std::string& paper_id, const std::string& output_path) {
    if (base_path.empty() && std::filesystem::path(output_path).is_relative()) {
        spdlog::error("[Fetcher]: No download directory for {}", output_path);
        return false;
    }
    try {
        auto url = ConstructPaperUrl(paper_id, "pdf");
        // Stream to disk instead of holding the whole PDF in memory.
        PdfWriter out(base_path / output_path);
        auto response =
            m_http->Download(url,
                             std::chrono::milliseconds(120000),
                             [&out](std::string_view chunk) { return out.Write(chunk); });

        if (response.status != 200) {
            spdlog::error(
                "[Fetcher]: Failed to download paper {}: HTTP {}", paper_id, response.status);
            return false;
        }
        if (!out.Commit()) {
            spdlog::error("[Fetcher]: Failed to download paper {}: {}", paper_id, out.Error());
            return false;
        }
        spdlog::info("[Fetcher]: Successfully downloaded paper {} to {}", paper_id, output_path);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[Fetcher]: Error downloading paper {}: {}", paper_id, e.what());
        return false;
    }
}

std::string Fetcher::NormalizeLink(const std::string& link) {
    if (link.size() > kMaxLinkLength)
        return link; // not a paper; IsArxivLink rejects it
    std::string result = link;

    // Normalize scheme to https
    if (result.compare(0, 7, "http://") == 0)
        result.replace(0, 7, "https://");

    // A PDF link names the same paper as its abstract page: store one form.
    static const std::regex pdf(R"(^(https://arxiv\.org)/pdf/(.+?)(?:\.pdf)?$)");
    std::smatch pm;
    if (std::regex_match(result, pm, pdf))
        result = pm[1].str() + "/abs/" + pm[2].str();

    // Strip the version suffix of a real arXiv identifier only — new style
    // (2401.12345v2) or old style (hep-ph/0101001v3). Stripping any trailing
    // "v<digits>" made the result depend on how often it was applied.
    static const std::regex versioned(
        R"(^(.*/abs/(?:\d{4}\.\d{4,5}|[A-Za-z-]+(?:\.[A-Za-z]{2})?/\d{7}))v\d+$)");
    std::smatch m;
    if (std::regex_match(result, m, versioned))
        result = m[1].str();

    return result;
}

bool Fetcher::IsArxivLink(const std::string& link) {
    constexpr std::string_view host = "https://arxiv.org/";
    if (link.size() > kMaxLinkLength || link.compare(0, host.size(), host) != 0)
        return false;
    const std::string_view path(link.c_str() + host.size());
    std::string_view id;
    for (std::string_view kind : {"abs/", "pdf/"})
        if (path.substr(0, kind.size()) == kind)
            id = path.substr(kind.size());
    if (id.empty() || id.front() == '/' || id.find("..") != std::string_view::npos)
        return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '/' ||
               c == '_';
    });
}

std::string Fetcher::ConstructPaperUrl(const std::string& paper_id,
                                       const std::string& format) const {
    return fmt::format("https://arxiv.org/{}/{}", format, paper_id);
}

std::optional<std::string> Fetcher::FetchFeeds() {
    spdlog::trace("[Fetcher]: Fetching articles for topics [{}]", fmt::join(m_topics, ", "));
    try {
        auto url = fmt::format("https://rss.arxiv.org/rss/{}", fmt::join(m_topics, "+"));

        auto response = m_http->Get(url, std::chrono::milliseconds(30000));

        if (response.status == 200) {
            return response.body;
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
            if (!IsArxivLink(article.link)) {
                spdlog::warn("[Fetcher]: Skipping RSS item with invalid link '{}'",
                             ForLog(article.link));
                continue;
            }
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

namespace {

// system_clock counts nanoseconds in 64 bits (about 1678–2262); anything
// outside a sane window would overflow from_time_t. arXiv dates start in 1991.
bool year_in_range(const std::tm& tm) { return tm.tm_year >= 0 && tm.tm_year <= 300; }

} // namespace

std::optional<Arxiv::time_point> Fetcher::ParseDate(const std::string& date) const {
    if (date.empty())
        return std::nullopt;

    // The live arXiv RSS feed emits RFC 822 dates, e.g.
    //   "Mon, 22 Jun 2026 00:00:00 -0400"
    // while the test fixtures (and some mirrors) use ISO 8601, e.g.
    //   "2024-03-25T12:00:00Z".
    // Detect the format by its first character: RFC 822 begins with the
    // weekday name (a letter), ISO 8601 begins with the 4-digit year.
    if (std::isalpha(static_cast<unsigned char>(date.front()))) {
        // RFC 822: "Day, DD Mon YYYY HH:MM:SS ±HHMM" (or a named zone).
        std::istringstream ss(date);
        ss.imbue(std::locale::classic());
        std::tm tm = {};
        ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
        if (ss.fail() || !year_in_range(tm))
            return std::nullopt;

        // The fields in tm are wall-clock time at the trailing zone offset.
        // timegm interprets them as UTC; we then subtract the offset to get
        // true UTC. Parse a trailing "±HHMM" offset if present (named zones
        // such as "GMT"/"UT" are treated as zero offset).
        std::time_t utc = timegm(&tm);
        std::string zone;
        ss >> zone;
        if (!zone.empty() && (zone.front() == '+' || zone.front() == '-') && zone.size() >= 5) {
            int sign = (zone.front() == '-') ? -1 : 1;
            try {
                int hh = std::stoi(zone.substr(1, 2));
                int mm = std::stoi(zone.substr(3, 2));
                utc -= sign * (hh * 3600 + mm * 60);
            } catch (...) {
                // Malformed offset — fall back to the zone-less interpretation.
            }
        }
        return std::chrono::system_clock::from_time_t(utc);
    }

    // ISO 8601: "YYYY-MM-DDTHH:MM:SS" (optionally suffixed with 'Z').
    std::istringstream ss(date);
    ss.imbue(std::locale::classic());
    std::tm tm = {};
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail() || !year_in_range(tm))
        return std::nullopt;
    // The ISO feed is UTC ('Z'); interpret the fields as UTC, not local time.
    return std::chrono::system_clock::from_time_t(timegm(&tm));
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

std::string Fetcher::FetchSinceWindowStart(const std::string& utc_date) const {
    std::tm tm{};
    if (!parse_ymd_prefix(utc_date, tm))
        return utc_date;
    tm.tm_mday -= announce_lag_days;
    timegm(&tm); // normalise the day rollover
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

std::vector<Article> Fetcher::MergeAnnouncementFirst(std::vector<Article> announcement,
                                                     std::vector<Article> backfill) {
    announcement.reserve(announcement.size() + backfill.size());
    announcement.insert(announcement.end(),
                        std::make_move_iterator(backfill.begin()),
                        std::make_move_iterator(backfill.end()));
    return announcement;
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
    // not silently dropped.  Papers already in the DB are upserted by
    // AddArticle, which never rewrites their date.
    // arXiv submittedDate query format: YYYYMMDDHHMI (e.g. 202605020000).
    // Start the window announce_lag_days before utc_date: papers are announced
    // (and thus first visible) a day or more after their submission date, so a
    // window anchored exactly at the last fetch would miss papers submitted just
    // before it but announced after. AddArticle's upsert absorbs the overlap.
    const std::string window_start = FetchSinceWindowStart(utc_date);
    std::tm from_tm{};
    parse_ymd_prefix(window_start, from_tm);
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

    for (int page = 0;; ++page) {
        if (page == kMaxApiPages)
            throw FetchError("arXiv API: more than " + std::to_string(kMaxApiPages) +
                             " pages; giving up");
        auto url = fmt::format("{}?search_query={}{}&start={}&max_results={}"
                               "&sortBy=submittedDate&sortOrder=descending",
                               ARXIV_API_URL,
                               topic_query,
                               date_filter,
                               start,
                               max_results);

        spdlog::info("[Fetcher]: FetchSince GET {}", url);
        // A failure part-way through paging must not look like a complete
        // (shorter) result: the caller would mark the missed days as fetched.
        HttpResponse resp;
        try {
            m_api_pacer->Wait();
            resp = m_http->Get(url, std::chrono::milliseconds(15000));
        } catch (const std::exception& e) {
            throw FetchError(std::string("arXiv API network error: ") + e.what());
        }
        if (resp.status != 200)
            throw FetchError("arXiv API returned HTTP " + std::to_string(resp.status));

        auto batch = ParseAtomFeed(resp.body);
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
    // up until a later open. This lets a single FetchSince call cover both
    // backfill and today, so callers need no separate Fetch().
    //
    // Announcement first: AddArticle keeps the date of whichever batch it sees
    // first, and today's papers must be dated today. See
    // MergeAnnouncementFirst.
    auto merged = MergeAnnouncementFirst(Fetch(), std::move(all_articles));

    spdlog::info("[Fetcher]: FetchSince got {} articles since {}", merged.size(), utc_date);
    return merged;
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
        // The API reports errors as an <entry> whose id is an errors URL.
        if (!IsArxivLink(article.link)) {
            spdlog::warn("[Fetcher]: Skipping Atom entry with id '{}'", ForLog(raw_link));
            continue;
        }

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
    if (!parse_ymd_prefix(date, tm) || !year_in_range(tm))
        return std::nullopt;
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

std::optional<std::string> Fetcher::InspireSearchUrl(const std::string& paper_id) {
    // New style (2401.12345) or old style (hep-ph/0101001, math.AG/0101001),
    // optionally versioned. Anything else could smuggle query syntax.
    static const std::regex arxiv_id(
        R"(^(?:\d{4}\.\d{4,5}|[A-Za-z-]+(?:\.[A-Za-z]{2})?/\d{7})(?:v\d+)?$)");
    if (paper_id.size() > kMaxLinkLength || !std::regex_match(paper_id, arxiv_id))
        return std::nullopt;
    std::string encoded;
    for (char c : paper_id)
        encoded += (c == '/') ? std::string("%2F") : std::string(1, c);
    return "https://inspirehep.net/api/literature?q=eprint%20" + encoded + "&fields=texkeys&size=1";
}

bool Fetcher::IsInspireUrl(const std::string& url) {
    constexpr std::string_view origin = "https://inspirehep.net/";
    return url.compare(0, origin.size(), origin) == 0;
}

std::string Fetcher::FetchBibTeX(const std::string& paper_id) {
    // --- 1. Try InspireHEP ---
    // Query the literature search API for the arXiv eprint.
    const auto inspire_search = InspireSearchUrl(paper_id);
    if (!inspire_search) {
        spdlog::warn("[Fetcher]: Not an arXiv id, skipping InspireHEP: {}", paper_id);
        return {};
    }
    try {
        auto search_resp = m_http->Get(*inspire_search, std::chrono::milliseconds(5000));

        if (search_resp.status == 200) {
            auto js = nlohmann::json::parse(search_resp.body);
            auto& hits = js.at("hits").at("hits");
            if (!hits.empty()) {
                // The hit object carries a links.bibtex URL
                std::string bibtex_url = hits[0].at("links").at("bibtex").get<std::string>();
                // The response chooses the next URL; only follow it to
                // InspireHEP itself.
                if (!IsInspireUrl(bibtex_url)) {
                    spdlog::warn("[Fetcher]: Ignoring BibTeX link outside InspireHEP: {}",
                                 bibtex_url);
                    return {};
                }

                auto bib_resp = m_http->Get(bibtex_url, std::chrono::milliseconds(5000));
                // It goes to the clipboard and into project .bib files: take
                // only something that looks like one entry.
                const auto first = bib_resp.body.find_first_not_of(" \t\r\n");
                const bool plausible = first != std::string::npos && bib_resp.body[first] == '@' &&
                                       bib_resp.body.size() <= kMaxBibTeXBytes;
                if (bib_resp.status == 200 && !plausible)
                    spdlog::warn("[Fetcher]: Ignoring InspireHEP answer that is not BibTeX for {}",
                                 paper_id);
                if (bib_resp.status == 200 && plausible) {
                    spdlog::info("[Fetcher]: Got InspireHEP BibTeX for {}", paper_id);
                    return bib_resp.body;
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
