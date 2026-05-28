// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/Article.hh"

#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"
#include "fixtures/test_data.hh"

using namespace Catch::Matchers;
using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock         = arxiv_tui::test::FetcherMock;

// ---------------------------------------------------------------------------
// DatabaseManager: followed_authors table
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::FollowAuthor / IsFollowingAuthor round-trip", "[author][db]") {
    Arxiv::DatabaseManager db(":memory:");

    SECTION("author not followed by default") {
        REQUIRE_FALSE(db.IsFollowingAuthor("Doe, John"));
    }

    SECTION("FollowAuthor makes IsFollowingAuthor return true") {
        db.FollowAuthor("Hébert, Rémi");
        REQUIRE(db.IsFollowingAuthor("Hébert, Rémi"));
    }

    SECTION("UnfollowAuthor removes the author") {
        db.FollowAuthor("Smith, Alice");
        db.UnfollowAuthor("Smith, Alice");
        REQUIRE_FALSE(db.IsFollowingAuthor("Smith, Alice"));
    }

    SECTION("author with single quote round-trips") {
        db.FollowAuthor("O'Brien, Patrick");
        REQUIRE(db.IsFollowingAuthor("O'Brien, Patrick"));
    }
}

TEST_CASE("DatabaseManager::GetFollowedAuthors returns all followed", "[author][db]") {
    Arxiv::DatabaseManager db(":memory:");

    db.FollowAuthor("Author A");
    db.FollowAuthor("Author B");
    db.FollowAuthor("Author C");

    auto authors = db.GetFollowedAuthors();
    REQUIRE(authors.size() == 3);
    REQUIRE(std::find(authors.begin(), authors.end(), "Author A") != authors.end());
    REQUIRE(std::find(authors.begin(), authors.end(), "Author B") != authors.end());
    REQUIRE(std::find(authors.begin(), authors.end(), "Author C") != authors.end());
}

TEST_CASE("DatabaseManager::GetFollowedAuthors returns empty when none followed", "[author][db]") {
    Arxiv::DatabaseManager db(":memory:");
    REQUIRE(db.GetFollowedAuthors().empty());
}

TEST_CASE("DatabaseManager::FollowAuthor is idempotent", "[author][db]") {
    Arxiv::DatabaseManager db(":memory:");
    db.FollowAuthor("Smith, Bob");
    db.FollowAuthor("Smith, Bob"); // second call should not duplicate
    REQUIRE(db.GetFollowedAuthors().size() == 1);
}

// ---------------------------------------------------------------------------
// AppCore: follow / unfollow / list
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
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    return std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));
}

TEST_CASE("AppCore::FollowAuthor / UnfollowAuthor dispatches to DB", "[author][appcore]") {
    DatabaseManagerMock* db     = nullptr;
    FetcherMock*         fetcher = nullptr;
    auto core = make_core(db, fetcher);

    SECTION("FollowAuthor calls DB") {
        REQUIRE_CALL(*db, FollowAuthor(std::string("Doe, John")));
        core->FollowAuthor("Doe, John");
    }

    SECTION("UnfollowAuthor calls DB") {
        REQUIRE_CALL(*db, UnfollowAuthor(std::string("Doe, John")));
        core->UnfollowAuthor("Doe, John");
    }
}

TEST_CASE("AppCore::GetFollowedAuthors delegates to DB", "[author][appcore]") {
    DatabaseManagerMock* db     = nullptr;
    FetcherMock*         fetcher = nullptr;
    auto core = make_core(db, fetcher);

    ALLOW_CALL(*db, GetFollowedAuthors())
        .RETURN(std::vector<std::string>{"Author A", "Author B"});

    auto authors = core->GetFollowedAuthors();
    REQUIRE(authors.size() == 2);
}

TEST_CASE("AppCore::GetArticlesForFollowedAuthors returns articles matching any followed author",
          "[author][appcore]") {
    DatabaseManagerMock* db     = nullptr;
    FetcherMock*         fetcher = nullptr;
    auto core = make_core(db, fetcher);

    auto articles = arxiv_tui::test::fixtures::sample_articles;
    // sample_articles[0].authors = "John Doe, Jane Smith"
    ALLOW_CALL(*db, GetFollowedAuthors())
        .RETURN(std::vector<std::string>{"John Doe"});
    ALLOW_CALL(*db, GetRecent(ANY(int))).RETURN(articles);

    auto results = core->GetArticlesForFollowedAuthors();
    // At least one article should match "John Doe"
    REQUIRE(!results.empty());
    for (const auto& a : results) {
        REQUIRE_THAT(a.authors, ContainsSubstring("Doe"));
    }
}

TEST_CASE("AppCore::GetArticlesForFollowedAuthors returns empty when no followed authors",
          "[author][appcore]") {
    DatabaseManagerMock* db     = nullptr;
    FetcherMock*         fetcher = nullptr;
    auto core = make_core(db, fetcher);

    ALLOW_CALL(*db, GetFollowedAuthors())
        .RETURN(std::vector<std::string>{});
    ALLOW_CALL(*db, GetRecent(ANY(int))).RETURN(arxiv_tui::test::fixtures::sample_articles);

    auto results = core->GetArticlesForFollowedAuthors();
    REQUIRE(results.empty());
}

// ---------------------------------------------------------------------------
// "Followed Authors" appears in filter options
// ---------------------------------------------------------------------------

TEST_CASE("AppCore filter options include 'Followed Authors'", "[author][appcore]") {
    DatabaseManagerMock* db     = nullptr;
    FetcherMock*         fetcher = nullptr;
    auto core = make_core(db, fetcher);

    auto filters = core->GetFilterOptions();
    bool found = false;
    for (const auto& f : filters)
        if (f.find("Followed") != std::string::npos) found = true;
    REQUIRE(found);
}
