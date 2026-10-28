// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"
#include "Arxiv/Article.hh"
#include "Arxiv/Config.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fixtures/test_data.hh"
#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"

using namespace Catch::Matchers;
using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock = arxiv_tui::test::FetcherMock;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::unique_ptr<Arxiv::AppCore> make_core(DatabaseManagerMock*& db_out,
                                                 FetcherMock*& fetcher_out) {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    db_out = db_ptr.get();
    fetcher_out = fet_ptr.get();
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    return std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));
}

static std::string read_file(const fs::path& p) {
    std::ifstream f(p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------------------
// Fetcher::FetchBibTeX mock
// ---------------------------------------------------------------------------

TEST_CASE("FetcherMock: FetchBibTeX can be mocked", "[bibtex][fetcher]") {
    FetcherMock fetcher;

    SECTION("Returns BibTeX string when InspireHEP succeeds") {
        const std::string expected_bib = R"(@article{Doe:2024abc,
  author = {Doe, John},
  title  = {{Sample Article}},
  eprint = {2403.12345},
  archivePrefix = {arXiv},
})";
        REQUIRE_CALL(fetcher, FetchBibTeX(std::string("2403.12345"))).RETURN(expected_bib);

        std::string bib = fetcher.FetchBibTeX("2403.12345");
        REQUIRE_THAT(bib, ContainsSubstring("@article"));
        REQUIRE_THAT(bib, ContainsSubstring("2403.12345"));
    }

    SECTION("Returns empty string when lookup fails") {
        REQUIRE_CALL(fetcher, FetchBibTeX(std::string("9999.00000"))).RETURN(std::string{});

        std::string bib = fetcher.FetchBibTeX("9999.00000");
        REQUIRE(bib.empty());
    }
}

// ---------------------------------------------------------------------------
// AppCore::ExportArticleBibTeX
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::ExportArticleBibTeX — single article", "[bibtex][appcore]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "bibtex_single_test.bib";
    fs::remove(tmp);

    auto articles = arxiv_tui::test::fixtures::sample_articles;
    const auto& art = articles[0]; // link = https://arxiv.org/abs/2403.12345

    const std::string inspire_bib = R"(@article{Doe:2024abc,
  author        = {Doe, John and Smith, Jane},
  title         = {{Sample Article Title}},
  eprint        = {2403.12345},
  archivePrefix = {arXiv},
  primaryClass  = {cs.AI},
  year          = {2024},
})";

    SECTION("writes InspireHEP BibTeX when fetch succeeds") {
        fetcher_ptr->setBibTeXResponse("2403.12345", inspire_bib);

        bool ok = core->ExportArticleBibTeX(art, tmp.string());
        REQUIRE(ok);
        REQUIRE(fs::exists(tmp));

        std::string content = read_file(tmp);
        REQUIRE_THAT(content, ContainsSubstring("@article"));
        REQUIRE_THAT(content, ContainsSubstring("2403.12345"));
        REQUIRE_THAT(content, ContainsSubstring("Sample Article Title"));
    }

    SECTION("falls back to constructed BibTeX when fetch returns empty") {
        fetcher_ptr->setBibTeXResponse("2403.12345", "");

        bool ok = core->ExportArticleBibTeX(art, tmp.string());
        REQUIRE(ok);
        REQUIRE(fs::exists(tmp));

        std::string content = read_file(tmp);
        REQUIRE_THAT(content, ContainsSubstring("@article"));
        REQUIRE_THAT(content, ContainsSubstring("2403.12345"));
        REQUIRE_THAT(content, ContainsSubstring("arXiv"));
    }

    SECTION("returns false when output path is unwritable") {
        fetcher_ptr->setBibTeXResponse("2403.12345", inspire_bib);

        bool ok = core->ExportArticleBibTeX(art, "/no_such_dir/out.bib");
        REQUIRE_FALSE(ok);
    }

    fs::remove(tmp);
}

// ---------------------------------------------------------------------------
// AppCore::ExportArticlesBibTeX — selection
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::ExportArticlesBibTeX — multiple articles", "[bibtex][appcore]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "bibtex_multi_test.bib";
    fs::remove(tmp);

    auto articles = arxiv_tui::test::fixtures::sample_articles;

    SECTION("writes one entry per article") {
        fetcher_ptr->setBibTeXResponse("2403.12345", ""); // fallback
        fetcher_ptr->setBibTeXResponse("2403.12346", ""); // fallback

        bool ok = core->ExportArticlesBibTeX(articles, tmp.string());
        REQUIRE(ok);
        REQUIRE(fs::exists(tmp));

        std::string content = read_file(tmp);
        // Both article IDs should appear
        REQUIRE_THAT(content, ContainsSubstring("2403.12345"));
        REQUIRE_THAT(content, ContainsSubstring("2403.12346"));
        // At least two @article entries
        std::size_t count = 0;
        std::size_t pos = 0;
        while ((pos = content.find("@article", pos)) != std::string::npos) {
            ++count;
            ++pos;
        }
        REQUIRE(count >= 2);
    }

    SECTION("empty article list writes empty file without error") {
        bool ok = core->ExportArticlesBibTeX({}, tmp.string());
        REQUIRE(ok);
        // File may be empty or contain only a header comment
        std::string content = read_file(tmp);
        REQUIRE_FALSE(content.find("@article") != std::string::npos);
    }

    SECTION("returns false for unwritable path") {
        bool ok = core->ExportArticlesBibTeX(articles, "/no_such_dir/out.bib");
        REQUIRE_FALSE(ok);
    }

    fs::remove(tmp);
}

// ---------------------------------------------------------------------------
// AppCore::ExportProjectBibTeX — project
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::ExportProjectBibTeX — project articles", "[bibtex][appcore]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "bibtex_project_test.bib";
    fs::remove(tmp);

    auto articles = arxiv_tui::test::fixtures::sample_articles;

    SECTION("exports all project articles to BibTeX") {
        ALLOW_CALL(*db_ptr, GetArticlesForProject(std::string("MyProject"))).RETURN(articles);
        fetcher_ptr->setBibTeXResponse("2403.12345", "");
        fetcher_ptr->setBibTeXResponse("2403.12346", "");

        bool ok = core->ExportProjectBibTeX("MyProject", tmp.string());
        REQUIRE(ok);
        REQUIRE(fs::exists(tmp));

        std::string content = read_file(tmp);
        REQUIRE_THAT(content, ContainsSubstring("2403.12345"));
        REQUIRE_THAT(content, ContainsSubstring("2403.12346"));
    }

    SECTION("empty project produces valid (empty) output") {
        ALLOW_CALL(*db_ptr, GetArticlesForProject(std::string("EmptyProj")))
            .RETURN(std::vector<Arxiv::Article>{});

        bool ok = core->ExportProjectBibTeX("EmptyProj", tmp.string());
        REQUIRE(ok);
    }

    SECTION("returns false for unwritable path") {
        ALLOW_CALL(*db_ptr, GetArticlesForProject(std::string("MyProject"))).RETURN(articles);

        bool ok = core->ExportProjectBibTeX("MyProject", "/no_such_dir/out.bib");
        REQUIRE_FALSE(ok);
    }

    fs::remove(tmp);
}

// ---------------------------------------------------------------------------
// Fallback BibTeX construction
// ---------------------------------------------------------------------------

TEST_CASE("Fallback BibTeX is well-formed", "[bibtex][fallback]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "bibtex_fallback_test.bib";
    fs::remove(tmp);

    auto articles = arxiv_tui::test::fixtures::sample_articles;
    const auto& art = articles[0];

    fetcher_ptr->setBibTeXResponse("2403.12345", "");

    bool ok = core->ExportArticleBibTeX(art, tmp.string());
    REQUIRE(ok);

    std::string content = read_file(tmp);

    SECTION("contains @article entry type") {
        REQUIRE_THAT(content, ContainsSubstring("@article{"));
    }
    SECTION("contains eprint field with arxiv ID") {
        REQUIRE_THAT(content, ContainsSubstring("eprint"));
        REQUIRE_THAT(content, ContainsSubstring("2403.12345"));
    }
    SECTION("contains archivePrefix = {arXiv}") {
        REQUIRE_THAT(content, ContainsSubstring("archivePrefix"));
        REQUIRE_THAT(content, ContainsSubstring("arXiv"));
    }
    SECTION("contains title field") {
        REQUIRE_THAT(content, ContainsSubstring("title"));
        REQUIRE_THAT(content, ContainsSubstring("Sample Article Title"));
    }
    SECTION("contains author field") {
        REQUIRE_THAT(content, ContainsSubstring("author"));
        REQUIRE_THAT(content, ContainsSubstring("Doe"));
    }

    fs::remove(tmp);
}

// ---------------------------------------------------------------------------
// One BibTeX builder (code review §4): the clipboard (GetBibtex) and file
// exports used two different fallback generators with different keys,
// quoting and time zones, and neither escaped BibTeX specials.
// ---------------------------------------------------------------------------

namespace {

std::string fallback_bibtex(const Arxiv::Article& article) {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);
    ALLOW_CALL(*fetcher_ptr, FetchBibTeX(trompeloeil::_)).RETURN(std::string{});
    return core->GetBibtex(article);
}

// Braces must balance, ignoring escaped ones (\{ \}).
bool braces_balanced(const std::string& s) {
    int depth = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\') {
            ++i;
            continue;
        }
        depth += s[i] == '{' ? 1 : s[i] == '}' ? -1 : 0;
        if (depth < 0)
            return false;
    }
    return depth == 0;
}

} // namespace

TEST_CASE("BibTeX: clipboard and file exports produce the same entry", "[bibtex][fallback]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);
    ALLOW_CALL(*fetcher_ptr, FetchBibTeX(trompeloeil::_)).RETURN(std::string{});
    const auto& art = arxiv_tui::test::fixtures::sample_articles[0];
    fs::path tmp = fs::temp_directory_path() / "bibtex_same_entry.bib";

    REQUIRE(core->ExportArticleBibTeX(art, tmp.string()));
    std::string file = read_file(tmp);
    std::string clip = core->GetBibtex(art);
    // The file may end with a separating newline.
    while (!file.empty() && file.back() == '\n')
        file.pop_back();
    while (!clip.empty() && clip.back() == '\n')
        clip.pop_back();
    REQUIRE(file == clip);
    fs::remove(tmp);
}

TEST_CASE("BibTeX: special characters are escaped", "[bibtex][fallback]") {
    auto art = arxiv_tui::test::fixtures::sample_articles[0];
    art.title = "50% of {top & bottom # quarks";

    const std::string bib = fallback_bibtex(art);

    SECTION("percent") { REQUIRE_THAT(bib, ContainsSubstring("50\\%")); }
    SECTION("ampersand") { REQUIRE_THAT(bib, ContainsSubstring("top \\& bottom")); }
    SECTION("hash") { REQUIRE_THAT(bib, ContainsSubstring("\\# quarks")); }
    SECTION("braces stay balanced") { REQUIRE(braces_balanced(bib)); }
}

TEST_CASE("BibTeX: the cite key only uses safe characters", "[bibtex][fallback]") {
    auto art = arxiv_tui::test::fixtures::sample_articles[0];
    art.authors = "Seán O'Brien-Smith, J. Doe";

    const std::string bib = fallback_bibtex(art);
    const std::string key = bib.substr(bib.find('{') + 1, bib.find(',') - bib.find('{') - 1);

    REQUIRE_FALSE(key.empty());
    for (char c : key)
        REQUIRE((std::isalnum(static_cast<unsigned char>(c)) || c == ':' || c == '_' || c == '.' ||
                 c == '-'));
}

TEST_CASE("BibTeX: the year is the UTC year, whatever the local time zone", "[bibtex][fallback]") {
    auto art = arxiv_tui::test::fixtures::sample_articles[0];
    art.date = std::chrono::system_clock::from_time_t(1735687800); // 2024-12-31 23:30 UTC

    const char* old_tz = std::getenv("TZ");
    const std::string saved = old_tz ? old_tz : "";
    ::setenv("TZ", "Pacific/Kiritimati", 1); // UTC+14: already 2025 locally
    ::tzset();
    const std::string bib = fallback_bibtex(art);
    if (old_tz)
        ::setenv("TZ", saved.c_str(), 1);
    else
        ::unsetenv("TZ");
    ::tzset();

    REQUIRE_THAT(bib, ContainsSubstring("2024"));
    REQUIRE_THAT(bib, !ContainsSubstring("2025"));
}
