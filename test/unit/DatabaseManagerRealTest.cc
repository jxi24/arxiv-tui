// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <Arxiv/Article.hh>
#include <Arxiv/DatabaseManager.hh>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fixtures/test_data.hh>
#include <sqlite3.h>
#include <unistd.h>

// Tests against the *real* DatabaseManager implementation (in-memory SQLite).
// No mocking — these verify actual SQL behaviour.

using namespace Arxiv;
using namespace arxiv_tui::test::fixtures;
using namespace Catch::Matchers;

// ---------------------------------------------------------------------------
// Article storage
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: article storage", "[database][real]") {
    DatabaseManager db(":memory:");

    SECTION("AddArticle and GetRecent round-trip") {
        db.AddArticle(sample_articles[0]);
        auto articles = db.GetRecent(-1);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].title == sample_articles[0].title);
        REQUIRE(articles[0].link == sample_articles[0].link);
    }

    SECTION("AddArticle with same link replaces the existing record") {
        db.AddArticle(sample_articles[0]);
        Article updated = sample_articles[0];
        updated.title = "Updated Title";
        db.AddArticle(updated);
        auto articles = db.GetRecent(-1);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].title == "Updated Title");
    }

    SECTION("AddArticle with single quote in title and abstract stores correctly") {
        Article a = sample_articles[0];
        a.link = "https://arxiv.org/abs/test.quote";
        a.title = "It's a test: O'Brien's paper";
        a.abstract = "The author's method yields O(n^2) results";
        db.AddArticle(a);
        auto articles = db.GetRecent(-1);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].title == a.title);
        REQUIRE(articles[0].abstract == a.abstract);
    }

    SECTION("GetRecent(-1) returns all articles regardless of age") {
        for (const auto& a : sample_articles)
            db.AddArticle(a);
        REQUIRE(db.GetRecent(-1).size() == sample_articles.size());
    }

    SECTION("GetRecent(1) excludes articles older than one day") {
        Article recent = sample_articles[0];
        recent.date = std::chrono::system_clock::now();
        db.AddArticle(recent);

        Article old = sample_articles[1];
        old.date = std::chrono::system_clock::now() - std::chrono::hours(48);
        db.AddArticle(old);

        auto today = db.GetRecent(1);
        REQUIRE(today.size() == 1);
        REQUIRE(today[0].link == recent.link);
    }
}

// ---------------------------------------------------------------------------
// Re-announcement: arXiv re-lists a paper whenever a new version is posted.
// The stored record must keep the *first-release* date and every piece of
// user-owned state; only the feed-owned metadata may change.
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: re-adding an article preserves its first-release date", "[database][real]") {
    DatabaseManager db(":memory:");

    Article original = sample_articles[0];
    original.date = std::chrono::system_clock::from_time_t(1700000000); // 2023-11-14
    db.AddArticle(original);

    // Same paper, re-announced today as a v2 replacement.
    Article replaced = original;
    replaced.date = std::chrono::system_clock::from_time_t(1800000000); // 2027-01-15
    replaced.is_replacement = true;
    replaced.title = "Sample Article Title (v2)";
    replaced.abstract = "Revised abstract.";

    SECTION("the original date survives the re-announcement") {
        db.AddArticle(replaced);
        auto articles = db.GetRecent(-1);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].date == original.date);
    }

    SECTION("feed-owned metadata is still refreshed") {
        db.AddArticle(replaced);
        auto articles = db.GetRecent(-1);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].title == "Sample Article Title (v2)");
        REQUIRE(articles[0].abstract == "Revised abstract.");
        REQUIRE(articles[0].is_replacement == true);
    }

    SECTION("a bookmark survives the re-announcement") {
        db.ToggleBookmark(original.link, true);
        db.AddArticle(replaced); // the feed never carries a bookmark flag
        auto articles = db.GetRecent(-1);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].bookmarked == true);
    }

    SECTION("read state survives the re-announcement") {
        db.MarkArticleRead(original.link);
        db.AddArticle(replaced);
        REQUIRE(db.GetUnreadArticles().empty());
    }

    SECTION("a rating survives the re-announcement") {
        db.SetRating(original.link, 4);
        db.AddArticle(replaced);
        REQUIRE(db.GetRating(original.link) == 4);
    }
}

// ---------------------------------------------------------------------------
// Insertion-time tracking — powers the New Articles view. "New to this user"
// is a question about when we first stored the paper, not about when it was
// submitted: the backfill query stamps each paper with its v1 submission date,
// which can predate the last session by days.
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: GetArticlesAddedSince filters on insertion time", "[database][real]") {
    DatabaseManager db(":memory:");

    // A paper submitted a month ago that we are only now learning about.
    Article backfilled = sample_articles[0];
    backfilled.date = std::chrono::system_clock::now() - std::chrono::hours(24 * 30);
    db.AddArticle(backfilled);

    SECTION("returns rows stored after the cutoff whatever their submission date") {
        auto rows = db.GetArticlesAddedSince(std::time(nullptr) - 60);
        REQUIRE(rows.size() == 1);
        REQUIRE(rows[0].link == backfilled.link);
    }

    SECTION("excludes rows stored before the cutoff") {
        REQUIRE(db.GetArticlesAddedSince(std::time(nullptr) + 60).empty());
    }
}

// ---------------------------------------------------------------------------
// Bookmarking
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: bookmarking", "[database][real]") {
    DatabaseManager db(":memory:");
    db.AddArticle(sample_articles[0]);

    SECTION("ToggleBookmark marks article as bookmarked") {
        db.ToggleBookmark(sample_articles[0].link, true);
        auto bookmarked = db.ListBookmarked();
        REQUIRE(bookmarked.size() == 1);
        REQUIRE(bookmarked[0].link == sample_articles[0].link);
    }

    SECTION("ToggleBookmark removes bookmark") {
        db.ToggleBookmark(sample_articles[0].link, true);
        db.ToggleBookmark(sample_articles[0].link, false);
        REQUIRE(db.ListBookmarked().empty());
    }

    SECTION("ToggleBookmark on nonexistent link is a no-op") {
        REQUIRE_NOTHROW(db.ToggleBookmark("https://doesnotexist", true));
        REQUIRE(db.ListBookmarked().empty());
    }
}

// ---------------------------------------------------------------------------
// Read / unread tracking
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: read/unread tracking", "[database][real]") {
    DatabaseManager db(":memory:");
    db.AddArticle(sample_articles[0]);
    db.AddArticle(sample_articles[1]);

    SECTION("Articles start unread") {
        auto articles = db.GetRecent(-1);
        for (const auto& a : articles)
            REQUIRE_FALSE(a.read);
    }

    SECTION("MarkArticleRead sets the read flag") {
        db.MarkArticleRead(sample_articles[0].link);
        auto articles = db.GetRecent(-1);
        auto it = std::find_if(articles.begin(), articles.end(), [](const auto& a) {
            return a.link == sample_articles[0].link;
        });
        REQUIRE(it != articles.end());
        REQUIRE(it->read);
    }

    SECTION("MarkArticleRead is idempotent") {
        db.MarkArticleRead(sample_articles[0].link);
        REQUIRE_NOTHROW(db.MarkArticleRead(sample_articles[0].link));
    }

    SECTION("GetUnreadArticles returns only unread articles") {
        db.MarkArticleRead(sample_articles[0].link);
        auto unread = db.GetUnreadArticles();
        REQUIRE(unread.size() == 1);
        REQUIRE(unread[0].link == sample_articles[1].link);
    }

    SECTION("GetUnreadArticles returns all when nothing is read") {
        REQUIRE(db.GetUnreadArticles().size() == 2);
    }
}

// ---------------------------------------------------------------------------
// Article pruning
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: PruneArticles", "[database][real]") {
    DatabaseManager db(":memory:");

    // Build an article with an old date (100 days ago)
    Article old = sample_articles[0];
    old.link = "https://arxiv.org/abs/old.0001";
    old.date = std::chrono::system_clock::now() - std::chrono::hours(24 * 100);
    db.AddArticle(old);
    db.AddArticle(sample_articles[1]); // recent

    SECTION("Old unprotected article is deleted") {
        db.PruneArticles(30);
        auto articles = db.GetRecent(-1);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].link == sample_articles[1].link);
    }

    SECTION("Old bookmarked article is preserved") {
        db.ToggleBookmark(old.link, true);
        db.PruneArticles(30);
        REQUIRE(db.GetRecent(-1).size() == 2);
    }

    SECTION("Old rated article is preserved") {
        db.SetRating(old.link, 4);
        db.PruneArticles(30);
        REQUIRE(db.GetRecent(-1).size() == 2);
    }

    SECTION("Old article in a project is preserved") {
        db.AddProject("Keepers");
        db.LinkArticleToProject(old.link, "Keepers");
        db.PruneArticles(30);
        REQUIRE(db.GetRecent(-1).size() == 2);
    }

    SECTION("PruneArticles(0) is a no-op") {
        db.PruneArticles(0);
        REQUIRE(db.GetRecent(-1).size() == 2);
    }
}

// ---------------------------------------------------------------------------
// Tags
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: tag management", "[database][real][tags]") {
    DatabaseManager db(":memory:");
    db.AddArticle(sample_articles[0]);
    db.AddArticle(sample_articles[1]);

    SECTION("AddTag / GetTags round-trip") {
        db.AddTag("quantum");
        db.AddTag("lattice");
        auto tags = db.GetTags();
        REQUIRE(tags.size() == 2);
        REQUIRE(std::find(tags.begin(), tags.end(), "quantum") != tags.end());
    }

    SECTION("RemoveTag removes the tag and all article associations") {
        db.AddTag("qcd");
        db.LinkArticleToTag(sample_articles[0].link, "qcd");
        db.RemoveTag("qcd");
        REQUIRE(db.GetTags().empty());
        REQUIRE(db.GetTagsForArticle(sample_articles[0].link).empty());
    }

    SECTION("LinkArticleToTag / GetTagsForArticle round-trip") {
        db.AddTag("higgs");
        db.LinkArticleToTag(sample_articles[0].link, "higgs");
        auto tags = db.GetTagsForArticle(sample_articles[0].link);
        REQUIRE(tags.size() == 1);
        REQUIRE(tags[0] == "higgs");
    }

    SECTION("UnlinkArticleFromTag removes association") {
        db.AddTag("top");
        db.LinkArticleToTag(sample_articles[0].link, "top");
        db.UnlinkArticleFromTag(sample_articles[0].link, "top");
        REQUIRE(db.GetTagsForArticle(sample_articles[0].link).empty());
    }

    SECTION("GetArticlesForTag returns tagged articles") {
        db.AddTag("bsm");
        db.LinkArticleToTag(sample_articles[0].link, "bsm");
        db.LinkArticleToTag(sample_articles[1].link, "bsm");
        auto articles = db.GetArticlesForTag("bsm");
        REQUIRE(articles.size() == 2);
    }

    SECTION("Linking the same tag twice is idempotent") {
        db.AddTag("dup");
        db.LinkArticleToTag(sample_articles[0].link, "dup");
        REQUIRE_NOTHROW(db.LinkArticleToTag(sample_articles[0].link, "dup"));
        REQUIRE(db.GetTagsForArticle(sample_articles[0].link).size() == 1);
    }
}

// ---------------------------------------------------------------------------
// Auto-update project .bib
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: project bib_path", "[database][real]") {
    DatabaseManager db(":memory:");
    db.AddProject("TestProj");

    SECTION("GetProjectBibPath returns empty by default") {
        REQUIRE(db.GetProjectBibPath("TestProj").empty());
    }

    SECTION("SetProjectBibPath / GetProjectBibPath round-trip") {
        db.SetProjectBibPath("TestProj", "/tmp/test.bib");
        REQUIRE(db.GetProjectBibPath("TestProj") == "/tmp/test.bib");
    }
}

// ---------------------------------------------------------------------------
// Article deletion
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: DeleteArticle", "[database][real]") {
    DatabaseManager db(":memory:");
    db.AddArticle(sample_articles[0]);
    db.AddArticle(sample_articles[1]);

    SECTION("Deleted article no longer appears in GetRecent") {
        db.DeleteArticle(sample_articles[0].link);
        auto articles = db.GetRecent(-1);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].link == sample_articles[1].link);
    }

    SECTION("Delete removes associated rating") {
        db.SetRating(sample_articles[0].link, 5);
        db.DeleteArticle(sample_articles[0].link);
        // Article gone, rating should be gone too (no FK violation on re-add)
        db.AddArticle(sample_articles[0]);
        REQUIRE(db.GetRating(sample_articles[0].link) == 0);
    }

    SECTION("Delete removes project membership") {
        db.AddProject("TestProject");
        db.LinkArticleToProject(sample_articles[0].link, "TestProject");
        db.DeleteArticle(sample_articles[0].link);
        auto articles = db.GetArticlesForProject("TestProject");
        REQUIRE(articles.empty());
    }

    SECTION("Delete removes project note") {
        db.AddProject("TestProject");
        db.SetProjectNote("TestProject", sample_articles[0].link, "my note");
        db.DeleteArticle(sample_articles[0].link);
        // Re-add article and verify note is gone
        db.AddArticle(sample_articles[0]);
        REQUIRE(db.GetProjectNote("TestProject", sample_articles[0].link).empty());
    }

    SECTION("Deleting a nonexistent link is a no-op") {
        REQUIRE_NOTHROW(db.DeleteArticle("https://doesnotexist"));
        REQUIRE(db.GetRecent(-1).size() == 2);
    }
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: SearchArticles", "[database][real]") {
    DatabaseManager db(":memory:");
    db.AddArticle(
        sample_articles[0]); // title: "Sample Article Title",  author: "John Doe, Jane Smith"
    db.AddArticle(
        sample_articles[1]); // title: "Another Test Article",  author: "Alice Johnson, Bob Wilson"

    SECTION("Search by title finds matching article") {
        auto results = db.SearchArticles("Sample Article", true, false, false);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].link == sample_articles[0].link);
    }

    SECTION("Search by authors finds matching article") {
        auto results = db.SearchArticles("Alice", false, true, false);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].link == sample_articles[1].link);
    }

    SECTION("Search by abstract finds matching article") {
        auto results = db.SearchArticles("testing purposes", false, false, true);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].link == sample_articles[0].link);
    }

    SECTION("Search across all fields returns union of all matching articles") {
        // "sample" appears in both articles' abstracts — all-field search returns both
        auto results_both = db.SearchArticles("sample", false, false, true);
        REQUIRE(results_both.size() == 2);

        // "John Doe" only appears in article[0]'s authors field
        auto results_one = db.SearchArticles("John Doe", true, true, true);
        REQUIRE(results_one.size() == 1);
        REQUIRE(results_one[0].link == sample_articles[0].link);
    }

    SECTION("Search is case-insensitive for ASCII") {
        auto results = db.SearchArticles("sample article", true, false, false);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].link == sample_articles[0].link);
    }

    SECTION("Search with no matching term returns empty") {
        auto results = db.SearchArticles("xyznonexistent99", true, true, true);
        REQUIRE(results.empty());
    }

    SECTION("Search with all fields disabled returns empty") {
        auto results = db.SearchArticles("sample", false, false, false);
        REQUIRE(results.empty());
    }
}

// ---------------------------------------------------------------------------
// Ratings
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: ratings", "[database][real]") {
    DatabaseManager db(":memory:");
    db.AddArticle(sample_articles[0]);
    db.AddArticle(sample_articles[1]);

    SECTION("GetRating for unrated article returns 0") {
        REQUIRE(db.GetRating(sample_articles[0].link) == 0);
    }

    SECTION("SetRating then GetRating round-trip") {
        db.SetRating(sample_articles[0].link, 4);
        REQUIRE(db.GetRating(sample_articles[0].link) == 4);
    }

    SECTION("SetRating overwrites previous rating") {
        db.SetRating(sample_articles[0].link, 2);
        db.SetRating(sample_articles[0].link, 5);
        REQUIRE(db.GetRating(sample_articles[0].link) == 5);
    }

    SECTION("GetRatedArticles returns only rated articles") {
        db.SetRating(sample_articles[0].link, 3);
        auto rated = db.GetRatedArticles();
        REQUIRE(rated.size() == 1);
        REQUIRE(rated[0].first.link == sample_articles[0].link);
        REQUIRE(rated[0].second == 3);
    }

    SECTION("GetRatedArticles returns empty when nothing is rated") {
        REQUIRE(db.GetRatedArticles().empty());
    }
}

// ---------------------------------------------------------------------------
// Project hierarchy
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: project hierarchy", "[database][real]") {
    DatabaseManager db(":memory:");
    db.AddProject("Physics");
    db.AddProject("Quantum");

    SECTION("GetProjectParent for top-level project returns empty string") {
        REQUIRE(db.GetProjectParent("Physics").empty());
    }

    SECTION("GetProjectParent for nonexistent project returns empty string") {
        REQUIRE(db.GetProjectParent("DoesNotExist").empty());
    }

    SECTION("SetProjectParent / GetProjectParent round-trip") {
        db.SetProjectParent("Quantum", "Physics");
        REQUIRE(db.GetProjectParent("Quantum") == "Physics");
    }

    SECTION("RemoveProject clears the parent field of its children") {
        db.SetProjectParent("Quantum", "Physics");
        db.RemoveProject("Physics");
        REQUIRE(db.GetProjectParent("Quantum").empty());
    }

    SECTION("RemoveProject also deletes it from the projects list") {
        db.RemoveProject("Physics");
        auto projects = db.GetProjects();
        REQUIRE(std::find(projects.begin(), projects.end(), "Physics") == projects.end());
    }
}

// ---------------------------------------------------------------------------
// Project notes
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: project notes", "[database][real]") {
    DatabaseManager db(":memory:");
    db.AddArticle(sample_articles[0]);
    db.AddProject("Research");

    SECTION("GetProjectNote for missing note returns empty string") {
        REQUIRE(db.GetProjectNote("Research", sample_articles[0].link).empty());
    }

    SECTION("SetProjectNote / GetProjectNote round-trip") {
        db.SetProjectNote("Research", sample_articles[0].link, "very interesting");
        REQUIRE(db.GetProjectNote("Research", sample_articles[0].link) == "very interesting");
    }

    SECTION("SetProjectNote overwrites an existing note") {
        db.SetProjectNote("Research", sample_articles[0].link, "first note");
        db.SetProjectNote("Research", sample_articles[0].link, "revised note");
        REQUIRE(db.GetProjectNote("Research", sample_articles[0].link) == "revised note");
    }

    SECTION("RemoveProject cascades to project notes") {
        db.SetProjectNote("Research", sample_articles[0].link, "note");
        db.RemoveProject("Research");
        db.AddProject("Research"); // recreate
        REQUIRE(db.GetProjectNote("Research", sample_articles[0].link).empty());
    }
}

// ---------------------------------------------------------------------------
// GetProjectsForArticle
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: GetProjectsForArticle", "[database][real]") {
    DatabaseManager db(":memory:");
    db.AddArticle(sample_articles[0]);
    db.AddProject("Alpha");
    db.AddProject("Beta");

    SECTION("Returns empty for article not linked to any project") {
        REQUIRE(db.GetProjectsForArticle(sample_articles[0].link).empty());
    }

    SECTION("Returns all linked projects") {
        db.LinkArticleToProject(sample_articles[0].link, "Alpha");
        db.LinkArticleToProject(sample_articles[0].link, "Beta");
        auto projects = db.GetProjectsForArticle(sample_articles[0].link);
        REQUIRE(projects.size() == 2);
        bool has_alpha = std::find(projects.begin(), projects.end(), "Alpha") != projects.end();
        bool has_beta = std::find(projects.begin(), projects.end(), "Beta") != projects.end();
        REQUIRE(has_alpha);
        REQUIRE(has_beta);
    }

    SECTION("After unlink, project is no longer listed") {
        db.LinkArticleToProject(sample_articles[0].link, "Alpha");
        db.UnlinkArticleFromProject(sample_articles[0].link, "Alpha");
        REQUIRE(db.GetProjectsForArticle(sample_articles[0].link).empty());
    }

    SECTION("Linking the same article-project pair twice is idempotent") {
        db.LinkArticleToProject(sample_articles[0].link, "Alpha");
        REQUIRE_NOTHROW(db.LinkArticleToProject(sample_articles[0].link, "Alpha"));
        REQUIRE(db.GetProjectsForArticle(sample_articles[0].link).size() == 1);
    }
}

// ---------------------------------------------------------------------------
// Link normalization migration
// Uses a temporary file-based SQLite DB so non-canonical links can be seeded
// before DatabaseManager's constructor runs its one-time migration.
// ---------------------------------------------------------------------------

namespace {

struct TempDb {
    std::filesystem::path path;
    sqlite3* handle = nullptr;

    TempDb() {
        // ctest runs each TEST_CASE in its own process, so the name must be
        // unique across processes as well as within one: std::rand() alone
        // hands every process the same sequence and the DBs collide.
        static int counter = 0;
        path = std::filesystem::temp_directory_path() /
               ("arxiv_migrate_test_" + std::to_string(::getpid()) + "_" +
                std::to_string(counter++) + ".db");
        sqlite3_open(path.c_str(), &handle);
    }
    ~TempDb() {
        if (handle)
            sqlite3_close(handle);
        std::filesystem::remove(path);
    }

    void exec(const char* sql) { sqlite3_exec(handle, sql, nullptr, nullptr, nullptr); }
};

} // namespace

TEST_CASE("DB migration: normalize article links", "[database][migration]") {
    TempDb tmp;

    // Seed the database with the pre-migration table layout + non-canonical links.
    tmp.exec(R"(CREATE TABLE articles (
        link TEXT PRIMARY KEY, title TEXT, authors TEXT, abstract TEXT,
        date INTEGER, bookmarked INTEGER DEFAULT 0,
        relevance_score REAL DEFAULT 0.0, category TEXT DEFAULT '',
        is_replacement INTEGER DEFAULT 0))");
    tmp.exec("CREATE TABLE projects (name TEXT PRIMARY KEY, parent TEXT DEFAULT '')");
    tmp.exec(R"(CREATE TABLE project_articles (
        project_name TEXT, article_link TEXT,
        PRIMARY KEY (project_name, article_link)))");
    tmp.exec(R"(CREATE TABLE article_ratings (
        article_link TEXT PRIMARY KEY, rating INTEGER NOT NULL))");
    tmp.exec(R"(CREATE TABLE project_notes (
        project_name TEXT, article_link TEXT, note TEXT,
        PRIMARY KEY (project_name, article_link)))");
    tmp.exec("CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL DEFAULT '')");

    SECTION("http link with version is renamed to canonical https form") {
        tmp.exec("INSERT INTO articles (link, title) VALUES "
                 "('http://arxiv.org/abs/2605.28788v1', 'Test Paper')");
        sqlite3_close(tmp.handle);
        tmp.handle = nullptr;

        DatabaseManager db(tmp.path.string());
        auto articles = db.GetRecent(-1);

        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].link == "https://arxiv.org/abs/2605.28788");
    }

    SECTION("canonical link already exists: bookmark merged from non-canonical") {
        // canonical exists but is not bookmarked; http version is bookmarked
        tmp.exec("INSERT INTO articles (link, title, bookmarked) VALUES "
                 "('https://arxiv.org/abs/2605.28788', 'Test Paper', 0)");
        tmp.exec("INSERT INTO articles (link, title, bookmarked) VALUES "
                 "('http://arxiv.org/abs/2605.28788v1', 'Test Paper', 1)");
        sqlite3_close(tmp.handle);
        tmp.handle = nullptr;

        DatabaseManager db(tmp.path.string());
        auto articles = db.GetRecent(-1);

        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].link == "https://arxiv.org/abs/2605.28788");
        REQUIRE(articles[0].bookmarked == true);
    }

    SECTION("canonical link already exists: rating merged from non-canonical") {
        tmp.exec("INSERT INTO articles (link, title) VALUES "
                 "('https://arxiv.org/abs/2605.28788', 'Test Paper')");
        tmp.exec("INSERT INTO articles (link, title) VALUES "
                 "('http://arxiv.org/abs/2605.28788v1', 'Test Paper')");
        tmp.exec("INSERT INTO article_ratings VALUES "
                 "('http://arxiv.org/abs/2605.28788v1', 4)");
        sqlite3_close(tmp.handle);
        tmp.handle = nullptr;

        DatabaseManager db(tmp.path.string());
        REQUIRE(db.GetRating("https://arxiv.org/abs/2605.28788") == 4);
    }

    SECTION("canonical link already exists: project link merged from non-canonical") {
        tmp.exec("INSERT INTO articles (link, title) VALUES "
                 "('https://arxiv.org/abs/2605.28788', 'Test Paper')");
        tmp.exec("INSERT INTO articles (link, title) VALUES "
                 "('http://arxiv.org/abs/2605.28788v1', 'Test Paper')");
        tmp.exec("INSERT INTO projects VALUES ('MyProject', '')");
        tmp.exec("INSERT INTO project_articles VALUES "
                 "('MyProject', 'http://arxiv.org/abs/2605.28788v1')");
        sqlite3_close(tmp.handle);
        tmp.handle = nullptr;

        DatabaseManager db(tmp.path.string());
        auto projects = db.GetProjectsForArticle("https://arxiv.org/abs/2605.28788");
        REQUIRE(projects.size() == 1);
        REQUIRE(projects[0] == "MyProject");
    }

    SECTION("migration is idempotent: running twice leaves DB unchanged") {
        tmp.exec("INSERT INTO articles (link, title) VALUES "
                 "('http://arxiv.org/abs/2605.28788v1', 'Test Paper')");
        sqlite3_close(tmp.handle);
        tmp.handle = nullptr;

        {
            DatabaseManager db1(tmp.path.string());
            REQUIRE(db1.GetRecent(-1).size() == 1);
        }
        // Second open should not fail or duplicate anything
        DatabaseManager db2(tmp.path.string());
        auto articles = db2.GetRecent(-1);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].link == "https://arxiv.org/abs/2605.28788");
    }
}

// ---------------------------------------------------------------------------
// Insertion time is stamped once, on first storage, and never refreshed.
// Uses a file-based DB so the timestamp can be backdated with raw SQL, keeping
// the assertions independent of wall-clock timing.
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: re-announcing an article does not refresh its insertion time",
          "[database][real]") {
    TempDb tmp;
    sqlite3_close(tmp.handle);
    tmp.handle = nullptr;

    DatabaseManager db(tmp.path.string());
    db.AddArticle(sample_articles[0]);

    sqlite3_open(tmp.path.c_str(), &tmp.handle);
    tmp.exec("UPDATE articles SET added_at = 1000");
    sqlite3_close(tmp.handle);
    tmp.handle = nullptr;

    REQUIRE(db.GetArticlesAddedSince(500).size() == 1);
    REQUIRE(db.GetArticlesAddedSince(2000).empty());

    // arXiv re-announces the paper as a v2 replacement — it is not new to us.
    Article replaced = sample_articles[0];
    replaced.is_replacement = true;
    replaced.title = "Updated Title";
    db.AddArticle(replaced);

    REQUIRE(db.GetArticlesAddedSince(2000).empty());
}

// ---------------------------------------------------------------------------
// The anchor is the instant the *previous* fetch finished, so anything stored
// at that instant belongs to that fetch and has already been seen. The tie is
// not hypothetical: added_at and the end-of-fetch stamp are both whole seconds,
// and the stamp is written immediately after the bulk insert, so the last rows
// of every fetch land in the same second as the anchor.
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: GetArticlesAddedSince excludes rows stored at the anchor instant",
          "[database][real]") {
    TempDb tmp;
    sqlite3_close(tmp.handle);
    tmp.handle = nullptr;

    DatabaseManager db(tmp.path.string());
    db.AddArticle(sample_articles[0]);

    sqlite3_open(tmp.path.c_str(), &tmp.handle);
    tmp.exec("UPDATE articles SET added_at = 1000");
    sqlite3_close(tmp.handle);
    tmp.handle = nullptr;

    SECTION("a row stored in the anchor second is not new") {
        REQUIRE(db.GetArticlesAddedSince(1000).empty());
    }

    SECTION("a row stored after the anchor is still new") {
        REQUIRE(db.GetArticlesAddedSince(999).size() == 1);
    }
}

TEST_CASE("DB migration: added_at backfills from the article date", "[database][migration]") {
    TempDb tmp;

    // Pre-migration layout: no added_at column at all.
    tmp.exec(R"(CREATE TABLE articles (
        link TEXT PRIMARY KEY, title TEXT, authors TEXT, abstract TEXT,
        date INTEGER, bookmarked INTEGER DEFAULT 0,
        relevance_score REAL DEFAULT 0.0, category TEXT DEFAULT '',
        is_replacement INTEGER DEFAULT 0))");
    tmp.exec("CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL DEFAULT '')");
    tmp.exec("INSERT INTO articles (link, title, date) VALUES "
             "('https://arxiv.org/abs/2605.28788', 'Old Paper', 1700000000)");
    sqlite3_close(tmp.handle);
    tmp.handle = nullptr;

    DatabaseManager db(tmp.path.string());

    REQUIRE(db.GetArticlesAddedSince(1699999999).size() == 1);
    REQUIRE(db.GetArticlesAddedSince(1700000001).empty());
}
