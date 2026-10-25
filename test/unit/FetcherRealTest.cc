// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <Arxiv/Fetcher.hh>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fixtures/test_data.hh>
#include <fstream>

using namespace Arxiv;
using namespace Catch::Matchers;
using namespace arxiv_tui::test::fixtures;

// Minimal Atom feed fragment used by several test cases.
static const std::string ATOM_FEED = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom"
      xmlns:arxiv="http://arxiv.org/schemas/atom">
  <entry>
    <id>http://arxiv.org/abs/2605.28788v1</id>
    <title>Quantum Corrections to Higgs Production</title>
    <summary>We compute NLO corrections to Higgs boson production.</summary>
    <author><name>Alice Smith</name></author>
    <author><name>Bob Jones</name></author>
    <published>2026-05-10T00:00:00Z</published>
    <arxiv:primary_category term="hep-ph"/>
    <arxiv:announce_type>new</arxiv:announce_type>
  </entry>
  <entry>
    <id>http://arxiv.org/abs/2605.28789v2</id>
    <title>Updated Lattice QCD Results</title>
    <summary>An update to our previous lattice calculation.</summary>
    <author><name>Carol Davis</name></author>
    <published>2026-05-09T00:00:00Z</published>
    <arxiv:primary_category term="hep-lat"/>
    <arxiv:announce_type>replace</arxiv:announce_type>
  </entry>
</feed>)";

// ---------------------------------------------------------------------------
// Fetcher construction
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher construction", "[fetcher][real]") {
    SECTION("Creates download directory if absent") {
        auto tmp = std::filesystem::temp_directory_path() / "arxiv_fetcher_test_dir";
        std::filesystem::remove_all(tmp);
        REQUIRE_NOTHROW(Fetcher({"hep-ph"}, tmp.string()));
        REQUIRE(std::filesystem::is_directory(tmp));
        std::filesystem::remove_all(tmp);
    }

    SECTION("Throws when base_path exists but is a file") {
        auto tmp = std::filesystem::temp_directory_path() / "arxiv_fetcher_file";
        std::ofstream(tmp.string()).close();
        REQUIRE_THROWS(Fetcher({"hep-ph"}, tmp.string()));
        std::filesystem::remove(tmp);
    }
}

// ---------------------------------------------------------------------------
// ParseFeed (RSS)
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher::ParseFeed (RSS)", "[fetcher][real][rss]") {
    auto tmp = std::filesystem::temp_directory_path() / "arxiv_fetcher_rss";
    std::filesystem::create_directories(tmp);
    Fetcher fetcher({"cs.AI"}, tmp.string());

    SECTION("Parses link, title, authors from RSS fixture") {
        auto articles = fetcher.ParseFeed(sample_rss_response);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].link == "https://arxiv.org/abs/2403.12345");
        REQUIRE_THAT(articles[0].title, ContainsSubstring("Sample Article Title"));
        REQUIRE_THAT(articles[0].authors, ContainsSubstring("John Doe"));
    }

    SECTION("Strips 'Abstract:' prefix from description") {
        std::string xml =
            R"(<?xml version="1.0"?><rss version="2.0" xmlns:dc="http://purl.org/dc/elements/1.1/"><channel><item>
            <title>Test</title><link>https://arxiv.org/abs/2403.99999</link>
            <description>Abstract: The actual abstract text.</description>
            <pubDate>2024-03-25T12:00:00Z</pubDate>
            <dc:creator>Author A</dc:creator>
        </item></channel></rss>)";
        auto articles = fetcher.ParseFeed(xml);
        REQUIRE(articles.size() == 1);
        REQUIRE_THAT(articles[0].abstract, ContainsSubstring("The actual abstract text"));
        REQUIRE_FALSE(articles[0].abstract.find("Abstract:") != std::string::npos);
    }

    SECTION("Normalizes http link with version suffix") {
        std::string xml =
            R"(<?xml version="1.0"?><rss version="2.0" xmlns:dc="http://purl.org/dc/elements/1.1/"><channel><item>
            <title>T</title><link>http://arxiv.org/abs/2403.11111v1</link>
            <description>Abstract: text</description>
            <pubDate>2024-03-25T12:00:00Z</pubDate>
            <dc:creator>Author</dc:creator>
        </item></channel></rss>)";
        auto articles = fetcher.ParseFeed(xml);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].link == "https://arxiv.org/abs/2403.11111");
    }

    SECTION("Returns empty vector for malformed XML") {
        auto articles = fetcher.ParseFeed("not xml at all");
        REQUIRE(articles.empty());
    }

    SECTION("Returns empty vector when channel element is missing") {
        auto articles = fetcher.ParseFeed(R"(<?xml version="1.0"?><rss version="2.0"/>)");
        REQUIRE(articles.empty());
    }

    std::filesystem::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// ParseAtomFeed
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher::ParseAtomFeed", "[fetcher][real][atom]") {
    auto tmp = std::filesystem::temp_directory_path() / "arxiv_fetcher_atom";
    std::filesystem::create_directories(tmp);
    Fetcher fetcher({"hep-ph"}, tmp.string());

    SECTION("Parses id, title, authors, category from Atom feed") {
        auto articles = fetcher.ParseAtomFeed(ATOM_FEED);
        REQUIRE(articles.size() == 2);
        REQUIRE(articles[0].link == "https://arxiv.org/abs/2605.28788");
        REQUIRE_THAT(articles[0].title, ContainsSubstring("Higgs"));
        REQUIRE_THAT(articles[0].authors, ContainsSubstring("Alice Smith"));
        REQUIRE_THAT(articles[0].authors, ContainsSubstring("Bob Jones"));
        REQUIRE(articles[0].category == "hep-ph");
    }

    SECTION("Marks replace-type entries as is_replacement") {
        auto articles = fetcher.ParseAtomFeed(ATOM_FEED);
        REQUIRE(articles.size() == 2);
        REQUIRE_FALSE(articles[0].is_replacement);
        REQUIRE(articles[1].is_replacement);
    }

    SECTION("Strips version suffix from id to canonical link") {
        auto articles = fetcher.ParseAtomFeed(ATOM_FEED);
        REQUIRE(articles[1].link == "https://arxiv.org/abs/2605.28789");
    }

    SECTION("Returns empty vector for malformed XML") {
        REQUIRE(fetcher.ParseAtomFeed("garbage").empty());
    }

    SECTION("Returns empty vector when feed element is missing") {
        REQUIRE(fetcher.ParseAtomFeed(R"(<?xml version="1.0"?><root/>)").empty());
    }

    std::filesystem::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// ParseDate (RSS pubDate)
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher::ParseDate", "[fetcher][real]") {
    Fetcher fetcher({"hep-ph"});

    SECTION("Parses valid ISO-8601 datetime") {
        auto tp = fetcher.ParseDate("2024-03-25T12:00:00Z");
        REQUIRE(tp.has_value());
    }

    SECTION("Parses real arXiv RSS RFC 822 pubDate") {
        // The live arXiv RSS feed emits RFC 822 dates, e.g.
        //   <pubDate>Mon, 22 Jun 2026 00:00:00 -0400</pubDate>
        auto tp = fetcher.ParseDate("Mon, 22 Jun 2026 00:00:00 -0400");
        REQUIRE(tp.has_value());

        // 2026-06-22 00:00:00 -0400 == 2026-06-22 04:00:00 UTC.
        std::time_t t = std::chrono::system_clock::to_time_t(tp.value());
        std::tm tm{};
        gmtime_r(&t, &tm);
        REQUIRE(tm.tm_year + 1900 == 2026);
        REQUIRE(tm.tm_mon + 1 == 6);
        REQUIRE(tm.tm_mday == 22);
        REQUIRE(tm.tm_hour == 4);
    }

    SECTION("Parses RFC 822 pubDate with GMT zone") {
        auto tp = fetcher.ParseDate("Fri, 19 Jun 2026 13:30:00 GMT");
        REQUIRE(tp.has_value());
        std::time_t t = std::chrono::system_clock::to_time_t(tp.value());
        std::tm tm{};
        gmtime_r(&t, &tm);
        REQUIRE(tm.tm_mday == 19);
        REQUIRE(tm.tm_hour == 13);
    }

    SECTION("Returns nullopt for empty string") {
        REQUIRE_FALSE(fetcher.ParseDate("").has_value());
    }

    SECTION("Returns nullopt for obviously invalid input") {
        REQUIRE_FALSE(fetcher.ParseDate("not-a-date").has_value());
    }
}

// ---------------------------------------------------------------------------
// FetchSinceWindowStart — announcement-lag look-back
//
// arXiv stamps and filters by submittedDate, but papers are *announced* (become
// visible) one or more days later. A paper submitted just before the last fetch
// but announced after it must still be picked up, so the submittedDate window
// must start before the requested date.
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher::FetchSinceWindowStart backs off by the announcement lag", "[fetcher][real]") {
    Fetcher fetcher({"hep-ph"});

    SECTION("Shifts the window start several days earlier than the requested date") {
        // Regression for the missed Friday paper: submitted 2026-06-17,
        // announced 2026-06-19, last fetch 2026-06-18. A window starting at
        // 2026-06-18 drops it; the window must start on or before 2026-06-17.
        std::string start = fetcher.FetchSinceWindowStart("2026-06-18");
        REQUIRE(start <= "2026-06-17");
        REQUIRE(start == "2026-06-13"); // 2026-06-18 minus 5 days
    }

    SECTION("Handles month boundaries") {
        REQUIRE(fetcher.FetchSinceWindowStart("2026-06-03") == "2026-05-29");
    }

    SECTION("Returns the input unchanged when it cannot be parsed") {
        REQUIRE(fetcher.FetchSinceWindowStart("bad") == "bad");
    }
}

// ---------------------------------------------------------------------------
// ParseAtomDate
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher::ParseAtomDate", "[fetcher][real]") {
    Fetcher fetcher({"hep-ph"});

    SECTION("Parses valid Atom date") {
        auto tp = fetcher.ParseAtomDate("2026-05-10T00:00:00Z");
        REQUIRE(tp.has_value());
    }

    SECTION("Returns nullopt for too-short string") {
        REQUIRE_FALSE(fetcher.ParseAtomDate("2026").has_value());
    }
}

// ---------------------------------------------------------------------------
// ReplaceLatexAccents
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher::ReplaceLatexAccents", "[fetcher][real][latex]") {
    Fetcher fetcher({"hep-ph"});

    SECTION("Replaces acute accent") { REQUIRE(fetcher.ReplaceLatexAccents("\\'e") == "é"); }

    SECTION("Replaces grave accent") { REQUIRE(fetcher.ReplaceLatexAccents("\\`a") == "à"); }

    SECTION("Replaces umlaut") { REQUIRE(fetcher.ReplaceLatexAccents("\\\"o") == "ö"); }

    SECTION("Replaces circumflex") { REQUIRE(fetcher.ReplaceLatexAccents("\\^u") == "û"); }

    SECTION("Leaves plain text unchanged") {
        REQUIRE(fetcher.ReplaceLatexAccents("hello world") == "hello world");
    }

    SECTION("Replaces multiple accents in one string") {
        auto result = fetcher.ReplaceLatexAccents("\\'e et \\`a");
        REQUIRE_THAT(result, ContainsSubstring("é"));
        REQUIRE_THAT(result, ContainsSubstring("à"));
    }
}

// ---------------------------------------------------------------------------
// LatexToMarkdown
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher::LatexToMarkdown", "[fetcher][real][latex]") {
    Fetcher fetcher({"hep-ph"});

    SECTION("\\textit{x} becomes *x*") {
        REQUIRE(fetcher.LatexToMarkdown("\\textit{word}") == "*word*");
    }

    SECTION("\\emph{x} becomes *x*") {
        REQUIRE(fetcher.LatexToMarkdown("\\emph{emphasis}") == "*emphasis*");
    }

    SECTION("\\textsl{x} becomes *x*") {
        REQUIRE(fetcher.LatexToMarkdown("\\textsl{slanted}") == "*slanted*");
    }

    SECTION("\\textbf{x} becomes **x**") {
        REQUIRE(fetcher.LatexToMarkdown("\\textbf{bold}") == "**bold**");
    }

    SECTION("\\texttt{x} becomes `x`") {
        REQUIRE(fetcher.LatexToMarkdown("\\texttt{code}") == "`code`");
    }

    SECTION("\\st{x} becomes ~~x~~") {
        REQUIRE(fetcher.LatexToMarkdown("\\st{struck}") == "~~struck~~");
    }

    SECTION("\\textsc{x} strips to x (no Markdown equivalent)") {
        REQUIRE(fetcher.LatexToMarkdown("\\textsc{SmallCaps}") == "SmallCaps");
    }

    SECTION("\\underline{x} strips to x") {
        REQUIRE(fetcher.LatexToMarkdown("\\underline{underlined}") == "underlined");
    }

    SECTION("$math$ is preserved unchanged") {
        REQUIRE(fetcher.LatexToMarkdown("energy $E=mc^2$ formula") == "energy $E=mc^2$ formula");
    }

    SECTION("Nested \\textbf{\\textit{x}} becomes ***x***") {
        REQUIRE(fetcher.LatexToMarkdown("\\textbf{\\textit{strong italic}}") ==
                "***strong italic***");
    }

    SECTION("Plain text is unchanged") {
        REQUIRE(fetcher.LatexToMarkdown("plain text") == "plain text");
    }

    SECTION("Inline usage: formatting inside a sentence") {
        auto result = fetcher.LatexToMarkdown("We study \\textit{CP} violation.");
        REQUIRE(result == "We study *CP* violation.");
    }
}

TEST_CASE("ParseFeed stores Markdown-formatted text", "[fetcher][real][rss]") {
    auto tmp = std::filesystem::temp_directory_path() / "arxiv_fetcher_md";
    std::filesystem::create_directories(tmp);
    Fetcher fetcher({"hep-ph"}, tmp.string());

    SECTION("\\textit in title is converted to *x* in stored article") {
        std::string xml =
            R"(<?xml version="1.0"?><rss version="2.0" xmlns:dc="http://purl.org/dc/elements/1.1/"><channel><item>
            <title>\textit{CP} violation at the LHC</title>
            <link>https://arxiv.org/abs/2403.11111</link>
            <description>Abstract: We measure \\textbf{coupling} constants.</description>
            <pubDate>2024-03-25T12:00:00Z</pubDate>
            <dc:creator>Author A</dc:creator>
        </item></channel></rss>)";
        auto articles = fetcher.ParseFeed(xml);
        REQUIRE(articles.size() == 1);
        REQUIRE_THAT(articles[0].title, ContainsSubstring("*CP*"));
        REQUIRE_THAT(articles[0].abstract, ContainsSubstring("**coupling**"));
    }

    std::filesystem::remove_all(tmp);
}

TEST_CASE("ParseAtomFeed stores Markdown-formatted text", "[fetcher][real][atom]") {
    auto tmp = std::filesystem::temp_directory_path() / "arxiv_fetcher_atom_md";
    std::filesystem::create_directories(tmp);
    Fetcher fetcher({"hep-ph"}, tmp.string());

    static const std::string ATOM_WITH_LATEX = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom"
      xmlns:arxiv="http://arxiv.org/schemas/atom">
  <entry>
    <id>http://arxiv.org/abs/2605.00001v1</id>
    <title>\textbf{Strong} force at \textit{high} energy</title>
    <summary>We study the \textit{non-perturbative} regime.</summary>
    <author><name>Alice Smith</name></author>
    <published>2026-05-10T00:00:00Z</published>
    <arxiv:primary_category term="hep-ph"/>
    <arxiv:announce_type>new</arxiv:announce_type>
  </entry>
</feed>)";

    SECTION("\\textbf in title becomes **x** in stored article") {
        auto articles = fetcher.ParseAtomFeed(ATOM_WITH_LATEX);
        REQUIRE(articles.size() == 1);
        REQUIRE_THAT(articles[0].title, ContainsSubstring("**Strong**"));
        REQUIRE_THAT(articles[0].abstract, ContainsSubstring("*non-perturbative*"));
    }

    std::filesystem::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// ConstructPaperUrl
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher::ConstructPaperUrl", "[fetcher][real]") {
    Fetcher fetcher({"hep-ph"});

    SECTION("Constructs abs URL") {
        REQUIRE(fetcher.ConstructPaperUrl("2403.12345", "abs") ==
                "https://arxiv.org/abs/2403.12345");
    }

    SECTION("Constructs pdf URL") {
        REQUIRE(fetcher.ConstructPaperUrl("2403.12345", "pdf") ==
                "https://arxiv.org/pdf/2403.12345");
    }
}

// ---------------------------------------------------------------------------
// MergeAnnouncementFirst — announcement wins the date on a duplicate link
//
// FetchSince returns two batches: the API backfill (dates = v1 *submission*
// date) and today's RSS announcement (dates = *announcement* date). A paper
// submitted two days ago but announced today appears in both. AddArticle
// upserts and deliberately never rewrites `date`, so the batch that reaches
// the DB *first* fixes the date forever. The announcement must therefore come
// first, otherwise today's papers are stored with a two-day-old date and drop
// out of every "today" view.
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher::MergeAnnouncementFirst puts the announcement ahead of the backfill",
          "[fetcher][real]") {
    Article announced;
    announced.link = "https://arxiv.org/abs/2608.00001";
    announced.title = "Announced today";
    announced.date = std::chrono::system_clock::from_time_t(1785888000); // 2026-08-05

    Article backfilled;
    backfilled.link = "https://arxiv.org/abs/2608.00001";
    backfilled.title = "Same paper, submission date";
    backfilled.date = std::chrono::system_clock::from_time_t(1785715200); // 2026-08-03

    Article older;
    older.link = "https://arxiv.org/abs/2608.00002";
    older.title = "Backfill only";
    older.date = std::chrono::system_clock::from_time_t(1785715200);

    SECTION("The duplicate link is emitted from the announcement batch first") {
        auto merged = Fetcher::MergeAnnouncementFirst({announced}, {backfilled, older});
        REQUIRE(merged.size() == 3);
        REQUIRE(merged[0].link == announced.link);
        REQUIRE(merged[0].title == "Announced today");
    }

    SECTION("Backfill-only articles are preserved") {
        auto merged = Fetcher::MergeAnnouncementFirst({announced}, {backfilled, older});
        auto it = std::find_if(
            merged.begin(), merged.end(), [&](const Article& a) { return a.link == older.link; });
        REQUIRE(it != merged.end());
    }

    SECTION("An empty announcement leaves the backfill untouched") {
        auto merged = Fetcher::MergeAnnouncementFirst({}, {backfilled, older});
        REQUIRE(merged.size() == 2);
        REQUIRE(merged[0].link == backfilled.link);
    }
}

// ---------------------------------------------------------------------------
// Regressions found in code review — each fails until the bug is fixed.
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher regression: arXiv API error entries are not stored as articles",
          "[fetcher][real][regression]") {
    // The arXiv API reports errors as an ordinary Atom <entry>.
    const std::string error_feed = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <id>http://arxiv.org/api/errors#incorrect_id_format_for_1234</id>
    <title>Error</title>
    <summary>incorrect id format for 1234</summary>
    <updated>2026-05-04T00:00:00-04:00</updated>
    <author><name>arXiv api core</name></author>
  </entry>
</feed>)";
    auto tmp = std::filesystem::temp_directory_path() / "arxiv_fetcher_regression";
    Fetcher fetcher({"hep-ph"}, tmp.string());

    REQUIRE(fetcher.ParseAtomFeed(error_feed).empty());
}

TEST_CASE("Fetcher regression: RSS items whose link is not an arXiv abstract URL are dropped",
          "[fetcher][real][regression]") {
    // Links end up in shell commands (xdg-open) and file names, and the RSS
    // feed is fetched over plain http, so anything that is not a canonical
    // arXiv abstract link must be rejected at the parser.
    auto feed_with_link = [](const std::string& link) {
        return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:dc="http://purl.org/dc/elements/1.1/"><channel><item>
  <title>T</title><link>)") +
               link + R"(</link>
  <description>Abstract: A</description>
  <pubDate>Mon, 25 Mar 2024 12:00:00 -0400</pubDate>
  <dc:creator>A. Author</dc:creator><category>hep-ph</category>
</item></channel></rss>)";
    };
    auto tmp = std::filesystem::temp_directory_path() / "arxiv_fetcher_regression";
    Fetcher fetcher({"hep-ph"}, tmp.string());

    SECTION("shell metacharacters") {
        REQUIRE(fetcher
                    .ParseFeed(feed_with_link("https://arxiv.org/abs/2403.1&quot;; touch "
                                              "/tmp/pwned; &quot;"))
                    .empty());
    }
    SECTION("foreign host") {
        REQUIRE(fetcher.ParseFeed(feed_with_link("https://evil.example/abs/2403.12345")).empty());
    }
}

TEST_CASE("Fetcher regression: NormalizeLink only strips a real version suffix",
          "[fetcher][real][regression]") {
    // Found by fuzz_text: stripping any trailing "v<digits>" made the result
    // depend on how many times it was applied.
    const std::string odd = "https://arxiv.org/abs/hep-ph/0101001v0202002v3";
    const std::string once = Fetcher::NormalizeLink(odd);

    SECTION("idempotent") { REQUIRE(Fetcher::NormalizeLink(once) == once); }
    SECTION("old-style id with version") {
        REQUIRE(Fetcher::NormalizeLink("http://arxiv.org/abs/hep-ph/0101001v3") ==
                "https://arxiv.org/abs/hep-ph/0101001");
    }
}

TEST_CASE("Fetcher regression: dates outside the representable range are rejected",
          "[fetcher][real][regression]") {
    // Found by fuzz_text (UBSan): year 0001 overflowed the nanosecond
    // system_clock duration in from_time_t.
    auto tmp = std::filesystem::temp_directory_path() / "arxiv_fetcher_regression";
    Fetcher fetcher({"hep-ph"}, tmp.string());

    SECTION("RFC 822") { REQUIRE_FALSE(fetcher.ParseDate("Mon, 01 Jan 0001 00:00:00 +0000")); }
    SECTION("ISO 8601") { REQUIRE_FALSE(fetcher.ParseDate("0001-01-01T00:00:00Z")); }
    SECTION("Atom") { REQUIRE_FALSE(fetcher.ParseAtomDate("0001-01-01T00:00:00Z")); }
}

// ---------------------------------------------------------------------------
// /pdf/ links are accepted as well as /abs/ ones, and normalised to /abs/ so
// the same paper is never stored twice.
// ---------------------------------------------------------------------------

TEST_CASE("Fetcher: /pdf/ links are recognised and normalised to /abs/", "[fetcher][links]") {
    SECTION("accepted as an arXiv link") {
        REQUIRE(Fetcher::IsArxivLink("https://arxiv.org/pdf/2401.12345"));
    }
    SECTION("abs links still accepted") {
        REQUIRE(Fetcher::IsArxivLink("https://arxiv.org/abs/2401.12345"));
    }
    SECTION("other paths rejected") {
        REQUIRE_FALSE(Fetcher::IsArxivLink("https://arxiv.org/list/hep-ph/new"));
    }
    SECTION("pdf with version and extension") {
        REQUIRE(Fetcher::NormalizeLink("http://arxiv.org/pdf/2401.12345v2.pdf") ==
                "https://arxiv.org/abs/2401.12345");
    }
    SECTION("old-style pdf") {
        REQUIRE(Fetcher::NormalizeLink("https://arxiv.org/pdf/hep-ph/0101001v1") ==
                "https://arxiv.org/abs/hep-ph/0101001");
    }
}

TEST_CASE("Fetcher: an RSS item with a /pdf/ link is kept", "[fetcher][links]") {
    auto tmp = std::filesystem::temp_directory_path() / "arxiv_fetcher_regression";
    Fetcher fetcher({"hep-ph"}, tmp.string());
    const std::string feed = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel><item>
  <title>T</title><link>https://arxiv.org/pdf/2403.12345v1</link>
  <description>Abstract: A</description>
  <pubDate>Mon, 25 Mar 2024 12:00:00 -0400</pubDate>
</item></channel></rss>)";

    auto articles = fetcher.ParseFeed(feed);
    REQUIRE(articles.size() == 1);
    REQUIRE(articles[0].link == "https://arxiv.org/abs/2403.12345");
}

// ---------------------------------------------------------------------------
// InspireHEP lookup: the paper id goes into a query string and the response
// names the next URL to fetch — both come from outside.
// ---------------------------------------------------------------------------

TEST_CASE("Fetcher::InspireSearchUrl", "[fetcher][inspire][regression]") {
    SECTION("new-style id") {
        REQUIRE(
            Fetcher::InspireSearchUrl("2401.12345") ==
            "https://inspirehep.net/api/literature?q=eprint%202401.12345&fields=texkeys&size=1");
    }
    SECTION("old-style id has its slash encoded") {
        REQUIRE(Fetcher::InspireSearchUrl("hep-ph/0101001") ==
                "https://inspirehep.net/api/"
                "literature?q=eprint%20hep-ph%2F0101001&fields=texkeys&size=1");
    }
    SECTION("rejects an id that would add query parameters") {
        REQUIRE_FALSE(Fetcher::InspireSearchUrl("2401.12345&size=1000").has_value());
    }
    SECTION("rejects an id with spaces") {
        REQUIRE_FALSE(Fetcher::InspireSearchUrl("2401.12345 or title x").has_value());
    }
    SECTION("rejects an empty id") { REQUIRE_FALSE(Fetcher::InspireSearchUrl("").has_value()); }
}

TEST_CASE("Fetcher::IsInspireUrl only accepts inspirehep.net over https",
          "[fetcher][inspire][regression]") {
    SECTION("the API's own bibtex link") {
        REQUIRE(Fetcher::IsInspireUrl("https://inspirehep.net/api/literature/123?format=bibtex"));
    }
    SECTION("another host") { REQUIRE_FALSE(Fetcher::IsInspireUrl("https://evil.example/x")); }
    SECTION("a host that only starts with inspirehep.net") {
        REQUIRE_FALSE(Fetcher::IsInspireUrl("https://inspirehep.net.evil.example/x"));
    }
    SECTION("user-info trick") {
        REQUIRE_FALSE(Fetcher::IsInspireUrl("https://inspirehep.net@evil.example/x"));
    }
    SECTION("plain http") {
        REQUIRE_FALSE(Fetcher::IsInspireUrl("http://inspirehep.net/api/literature/1"));
    }
    SECTION("a local file") { REQUIRE_FALSE(Fetcher::IsInspireUrl("file:///etc/passwd")); }
}
