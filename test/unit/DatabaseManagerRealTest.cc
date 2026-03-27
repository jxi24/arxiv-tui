#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <Arxiv/DatabaseManager.hh>
#include <Arxiv/Article.hh>
#include <fixtures/test_data.hh>
#include <chrono>

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
        REQUIRE(articles[0].link  == sample_articles[0].link);
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
        a.link     = "https://arxiv.org/abs/test.quote";
        a.title    = "It's a test: O'Brien's paper";
        a.abstract = "The author's method yields O(n^2) results";
        db.AddArticle(a);
        auto articles = db.GetRecent(-1);
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].title    == a.title);
        REQUIRE(articles[0].abstract == a.abstract);
    }

    SECTION("GetRecent(-1) returns all articles regardless of age") {
        for (const auto& a : sample_articles) db.AddArticle(a);
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
// Search
// ---------------------------------------------------------------------------
TEST_CASE("Real DB: SearchArticles", "[database][real]") {
    DatabaseManager db(":memory:");
    db.AddArticle(sample_articles[0]);  // title: "Sample Article Title",  author: "John Doe, Jane Smith"
    db.AddArticle(sample_articles[1]);  // title: "Another Test Article",  author: "Alice Johnson, Bob Wilson"

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
        db.AddProject("Research");  // recreate
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
        bool has_beta  = std::find(projects.begin(), projects.end(), "Beta")  != projects.end();
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
