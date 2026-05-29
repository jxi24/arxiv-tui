// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <Arxiv/Fetcher.hh>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
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

    SECTION("Returns nullopt for empty string") {
        REQUIRE_FALSE(fetcher.ParseDate("").has_value());
    }

    SECTION("Returns nullopt for obviously invalid input") {
        REQUIRE_FALSE(fetcher.ParseDate("not-a-date").has_value());
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
// StyleLatex
// ---------------------------------------------------------------------------
TEST_CASE("Fetcher::StyleLatex", "[fetcher][real][latex]") {
    Fetcher fetcher({"hep-ph"});

    SECTION("Strips \\textit{} completely") {
        REQUIRE(fetcher.StyleLatex("\\textit{hello}") == "hello");
    }

    SECTION("Strips \\textbf{} completely") {
        REQUIRE(fetcher.StyleLatex("\\textbf{bold}") == "bold");
    }

    SECTION("Strips nested formatting commands") {
        REQUIRE(fetcher.StyleLatex("\\textit{\\textbf{word}}") == "word");
    }

    SECTION("Leaves plain text unchanged") {
        REQUIRE(fetcher.StyleLatex("plain text") == "plain text");
    }

    SECTION("Replaces $...$ math delimiters") {
        auto result = fetcher.StyleLatex("Energy is $E=mc^2$.");
        REQUIRE_THAT(result, ContainsSubstring("E=mc^2"));
    }
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
