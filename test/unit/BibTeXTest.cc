// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/Article.hh"

#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"
#include "fixtures/test_data.hh"

using namespace Catch::Matchers;
using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock         = arxiv_tui::test::FetcherMock;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::unique_ptr<Arxiv::AppCore> make_core(
    DatabaseManagerMock*& db_out,
    FetcherMock*&         fetcher_out)
{
    auto db_ptr  = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    db_out      = db_ptr.get();
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
        REQUIRE_CALL(fetcher, FetchBibTeX(std::string("2403.12345")))
            .RETURN(expected_bib);

        std::string bib = fetcher.FetchBibTeX("2403.12345");
        REQUIRE_THAT(bib, ContainsSubstring("@article"));
        REQUIRE_THAT(bib, ContainsSubstring("2403.12345"));
    }

    SECTION("Returns empty string when lookup fails") {
        REQUIRE_CALL(fetcher, FetchBibTeX(std::string("9999.00000")))
            .RETURN(std::string{});

        std::string bib = fetcher.FetchBibTeX("9999.00000");
        REQUIRE(bib.empty());
    }
}

// ---------------------------------------------------------------------------
// AppCore::ExportArticleBibTeX
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::ExportArticleBibTeX — single article", "[bibtex][appcore]") {
    DatabaseManagerMock* db_ptr     = nullptr;
    FetcherMock*         fetcher_ptr = nullptr;
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
    DatabaseManagerMock* db_ptr     = nullptr;
    FetcherMock*         fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "bibtex_multi_test.bib";
    fs::remove(tmp);

    auto articles = arxiv_tui::test::fixtures::sample_articles;

    SECTION("writes one entry per article") {
        fetcher_ptr->setBibTeXResponse("2403.12345", "");  // fallback
        fetcher_ptr->setBibTeXResponse("2403.12346", "");  // fallback

        bool ok = core->ExportArticlesBibTeX(articles, tmp.string());
        REQUIRE(ok);
        REQUIRE(fs::exists(tmp));

        std::string content = read_file(tmp);
        // Both article IDs should appear
        REQUIRE_THAT(content, ContainsSubstring("2403.12345"));
        REQUIRE_THAT(content, ContainsSubstring("2403.12346"));
        // At least two @article entries
        std::size_t count = 0;
        std::size_t pos   = 0;
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
    DatabaseManagerMock* db_ptr     = nullptr;
    FetcherMock*         fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "bibtex_project_test.bib";
    fs::remove(tmp);

    auto articles = arxiv_tui::test::fixtures::sample_articles;

    SECTION("exports all project articles to BibTeX") {
        ALLOW_CALL(*db_ptr, GetArticlesForProject(std::string("MyProject")))
            .RETURN(articles);
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
        ALLOW_CALL(*db_ptr, GetArticlesForProject(std::string("MyProject")))
            .RETURN(articles);

        bool ok = core->ExportProjectBibTeX("MyProject", "/no_such_dir/out.bib");
        REQUIRE_FALSE(ok);
    }

    fs::remove(tmp);
}

// ---------------------------------------------------------------------------
// Fallback BibTeX construction
// ---------------------------------------------------------------------------

TEST_CASE("Fallback BibTeX is well-formed", "[bibtex][fallback]") {
    DatabaseManagerMock* db_ptr     = nullptr;
    FetcherMock*         fetcher_ptr = nullptr;
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
