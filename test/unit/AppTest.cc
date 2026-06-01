// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <Arxiv/AppCore.hh>
#include <Arxiv/Config.hh>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdio>
#include <fixtures/test_data.hh>
#include <fstream>
#include <mocks/DatabaseManagerMock.hh>
#include <mocks/FetcherMock.hh>
#include <unistd.h>

using namespace Arxiv;
using namespace arxiv_tui::test;
using namespace arxiv_tui::test::fixtures;
using namespace Catch::Matchers;

TEST_CASE("AppCore initialization", "[app]") {
    SECTION("Should initialize with config") {
        Config config("test/fixtures/test_config.yml");
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        // GetRecent(-1) is called twice by the constructor:
        // once in FetchArticles() and once in the ranker-load fallback.
        // Use ALLOW_CALL to permit any number of calls.
        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);

        Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

        auto articles = core.GetCurrentArticles();
        auto filter_options = core.GetFilterOptions();

        REQUIRE(!filter_options.empty());
        REQUIRE(filter_options[0] == "All Articles");
        REQUIRE(filter_options[1] == "Bookmarks");
        REQUIRE(filter_options[2] == "Today");
        REQUIRE(articles.size() == sample_articles.size());
    }
}

TEST_CASE("AppCore article management", "[app]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();
    auto* fetcher_ptr = fetcher.get();

    // Set up expectations in the test scope
    ALLOW_CALL(*fetcher_ptr, Fetch()).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, AddArticle(trompeloeil::_));
    ALLOW_CALL(*db_ptr, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, ListBookmarked()).RETURN(std::vector<Arxiv::Article>{});
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
    ALLOW_CALL(*db_ptr, ToggleBookmark(trompeloeil::_, trompeloeil::_));

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Should handle article filtering") {
        // Set up mock responses for different filter scenarios
        std::vector<Arxiv::Article> bookmarked_articles = {sample_articles[0]};
        std::vector<Arxiv::Article> today_articles = {sample_articles[0], sample_articles[1]};

        db_ptr->setBookmarkedArticles(bookmarked_articles);
        db_ptr->setArticles(today_articles);

        core.SetFilterIndex(Arxiv::AppCore::FilterView::All);
        auto all_articles = core.GetCurrentArticles();
        REQUIRE(all_articles.size() == sample_articles.size());

        core.SetFilterIndex(Arxiv::AppCore::FilterView::Bookmarks);
        auto filtered_bookmarked = core.GetCurrentArticles();
        REQUIRE(filtered_bookmarked.size() == bookmarked_articles.size());

        core.SetFilterIndex(Arxiv::AppCore::FilterView::Today);
        auto filtered_today = core.GetCurrentArticles();
        REQUIRE(filtered_today.size() == sample_articles.size());
    }

    SECTION("Should handle article bookmarking") {
        auto articles = core.GetCurrentArticles();
        REQUIRE(!articles.empty());
        std::string article_link = articles[0].link;
        bool initial_state = core.IsArticleBookmarked(article_link);

        core.ToggleBookmark(article_link);
        REQUIRE(core.IsArticleBookmarked(article_link) != initial_state);

        core.ToggleBookmark(article_link);
        REQUIRE(core.IsArticleBookmarked(article_link) == initial_state);
    }
}

TEST_CASE("AppCore project management", "[app]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();
    auto* fetcher_ptr = fetcher.get();

    std::vector<std::string> mock_projects;
    std::vector<Arxiv::Article> mock_project_articles;

    ALLOW_CALL(*fetcher_ptr, Fetch()).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, AddArticle(trompeloeil::_));
    ALLOW_CALL(*db_ptr, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, ListBookmarked()).RETURN(std::vector<Arxiv::Article>{});
    ALLOW_CALL(*db_ptr, GetProjects()).LR_RETURN(mock_projects);
    ALLOW_CALL(*db_ptr, AddProject(trompeloeil::_));
    ALLOW_CALL(*db_ptr, RemoveProject(trompeloeil::_));
    ALLOW_CALL(*db_ptr, LinkArticleToProject(trompeloeil::_, trompeloeil::_));
    ALLOW_CALL(*db_ptr, UnlinkArticleFromProject(trompeloeil::_, trompeloeil::_));
    ALLOW_CALL(*db_ptr, GetArticlesForProject(trompeloeil::_)).LR_RETURN(mock_project_articles);

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Should handle project creation and removal") {
        std::string project_name = "Test Project";
        auto initial_projects = core.GetProjects();

        mock_projects = {project_name};
        core.AddProject(project_name);
        auto projects = core.GetProjects();
        REQUIRE(projects.size() == initial_projects.size() + 1);
        REQUIRE(std::find(projects.begin(), projects.end(), project_name) != projects.end());

        mock_projects = {};
        core.RemoveProject(project_name);
        projects = core.GetProjects();
        REQUIRE(projects.size() == initial_projects.size());
        REQUIRE(std::find(projects.begin(), projects.end(), project_name) == projects.end());
    }

    SECTION("Should handle article-project linking") {
        auto articles = core.GetCurrentArticles();
        REQUIRE(!articles.empty());
        std::string article_link = articles[0].link;
        std::string project_name = "Test Project";

        mock_projects = {project_name};
        mock_project_articles = {articles[0]};

        core.AddProject(project_name);
        core.LinkArticleToProject(article_link, project_name);

        auto project_articles = core.GetArticlesForProject(project_name);
        REQUIRE(std::find_if(
                    project_articles.begin(), project_articles.end(), [&](const Arxiv::Article& a) {
                        return a.link == article_link;
                    }) != project_articles.end());

        mock_project_articles = {};
        core.UnlinkArticleFromProject(article_link, project_name);
        project_articles = core.GetArticlesForProject(project_name);
        REQUIRE(std::find_if(
                    project_articles.begin(), project_articles.end(), [&](const Arxiv::Article& a) {
                        return a.link == article_link;
                    }) == project_articles.end());
    }
}

TEST_CASE("AppCore state management", "[app]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();
    auto* fetcher_ptr = fetcher.get();

    ALLOW_CALL(*fetcher_ptr, Fetch()).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, AddArticle(trompeloeil::_));
    ALLOW_CALL(*db_ptr, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, ListBookmarked()).RETURN(std::vector<Arxiv::Article>{});
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Should handle article index changes") {
        auto articles = core.GetCurrentArticles();
        REQUIRE(!articles.empty());
        int initial_index = core.GetArticleIndex();
        core.SetArticleIndex(initial_index + 1);
        REQUIRE(core.GetArticleIndex() == initial_index + 1);
    }

    SECTION("Should handle filter index changes") {
        // Switch from default (NewArticles) to All, then back.
        core.SetFilterIndex(Arxiv::AppCore::FilterView::All);
        REQUIRE(core.GetFilterIndex() == static_cast<int>(Arxiv::AppCore::FilterView::All));
        core.SetFilterIndex(Arxiv::AppCore::FilterView::NewArticles);
        REQUIRE(core.GetFilterIndex() == static_cast<int>(Arxiv::AppCore::FilterView::NewArticles));
    }
}

TEST_CASE("AppCore rating and recommendation", "[app][ranking]") {
    Config config("test/fixtures/test_config.yml");
    // Point ranker storage at a unique tmp path so the test is hermetic:
    // a stale ranker.bin from a prior run (or another test) must not cause
    // IsRankerTrained() to spuriously return true, and our writes must not
    // pollute the next test's cwd.
    config.set_ranker_file("/tmp/arxiv_tui_apptest_ranker_" + std::to_string(::getpid()) + ".bin");
    std::remove(config.get_ranker_file().c_str());

    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();
    auto* fetcher_ptr = fetcher.get();

    fetcher_ptr->setFetchResponse(sample_articles);
    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("RateArticle stores rating and retrieves it") {
        auto articles = core.GetCurrentArticles();
        REQUIRE_FALSE(articles.empty());
        const std::string& link = articles[0].link;

        // Set up expectation: SetRating will be called
        REQUIRE_CALL(*db_ptr, SetRating(link, 4));
        // After rating, GetRatedArticles is called again for retraining
        Arxiv::DatabaseManager::RatedArticleList rated = {{articles[0], 4}};
        ALLOW_CALL(*db_ptr, GetRatedArticles()).RETURN(rated);
        ALLOW_CALL(*db_ptr, GetRecent(-1)).RETURN(sample_articles);

        core.RateArticle(link, 4);

        // GetRating should return the mocked value
        ALLOW_CALL(*db_ptr, GetRating(link)).RETURN(4);
        REQUIRE(core.GetArticleRating(link) == 4);
    }

    SECTION("Rating out of range [1,5] is rejected") {
        auto articles = core.GetCurrentArticles();
        REQUIRE_FALSE(articles.empty());
        const std::string& link = articles[0].link;

        // SetRating should NOT be called for invalid ratings
        FORBID_CALL(*db_ptr, SetRating(link, ANY(int)));

        core.RateArticle(link, 0);
        core.RateArticle(link, 6);
    }

    SECTION("Recommended filter is present in filter options") {
        auto options = core.GetFilterOptions();
        auto it = std::find(options.begin(), options.end(), "Recommended");
        REQUIRE(it != options.end());
        // Recommended should be at index 5
        REQUIRE(std::distance(options.begin(), it) == 5);
    }

    SECTION("Recommend threshold getter and setter") {
        float default_threshold = core.GetRecommendThreshold();
        REQUIRE(default_threshold > 0.0f);
        REQUIRE(default_threshold <= 5.0f);

        ALLOW_CALL(*db_ptr, GetRecent(1)).RETURN(sample_articles);
        core.SetRecommendThreshold(4.0f);
        REQUIRE(core.GetRecommendThreshold() == 4.0f);
    }

    SECTION("GetPredictedScore returns 0 when ranker is untrained") {
        auto articles = core.GetCurrentArticles();
        REQUIRE_FALSE(articles.empty());
        // No ratings provided, so ranker is untrained
        REQUIRE_FALSE(core.IsRankerTrained());
        REQUIRE(core.GetPredictedScore(articles[0]) == 0.0f);
    }

    SECTION("RateSelected rates all selected articles with the given rating") {
        auto articles = core.GetCurrentArticles();
        REQUIRE(articles.size() >= 2);
        const std::string& link0 = articles[0].link;
        const std::string& link1 = articles[1].link;

        core.ToggleSelection(link0);
        core.ToggleSelection(link1);

        ALLOW_CALL(*db_ptr, GetRecent(-1)).RETURN(sample_articles);
        REQUIRE_CALL(*db_ptr, SetRating(link0, 3));
        REQUIRE_CALL(*db_ptr, SetRating(link1, 3));

        core.RateSelected(3);
    }

    SECTION("RateSelected with no selection rates the focused article") {
        auto articles = core.GetCurrentArticles();
        REQUIRE_FALSE(articles.empty());
        const std::string& link = articles[0].link;

        REQUIRE(core.GetSelectionCount() == 0);
        ALLOW_CALL(*db_ptr, GetRecent(-1)).RETURN(sample_articles);
        REQUIRE_CALL(*db_ptr, SetRating(link, 2));

        core.RateSelected(2);
    }

    SECTION("RateSelected out of range is rejected") {
        auto articles = core.GetCurrentArticles();
        REQUIRE_FALSE(articles.empty());
        const std::string& link = articles[0].link;
        core.ToggleSelection(link);

        FORBID_CALL(*db_ptr, SetRating(link, ANY(int)));

        core.RateSelected(0);
        core.RateSelected(6);
    }
}

TEST_CASE("AppCore sub-project hierarchy", "[app][projects]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({});

    SECTION("Sub-project appears indented in filter options") {
        // parent "Physics" with child "Quantum"
        db_ptr->setProjects({"Physics", "Quantum"});
        ALLOW_CALL(*db_ptr, GetProjectParent(std::string("Physics"))).RETURN(std::string{});
        ALLOW_CALL(*db_ptr, GetProjectParent(std::string("Quantum")))
            .RETURN(std::string("Physics"));

        Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

        auto options = core.GetFilterOptions();
        // "Physics" should appear somewhere after the 5 built-in filters
        auto phys_it = std::find(options.begin(), options.end(), "Physics");
        REQUIRE(phys_it != options.end());
        // "  Quantum" (indented) should follow
        auto quantum_it = std::find(options.begin(), options.end(), "  Quantum");
        REQUIRE(quantum_it != options.end());
        // Quantum must come after Physics
        REQUIRE(std::distance(options.begin(), phys_it) <
                std::distance(options.begin(), quantum_it));
    }

    SECTION("GetProjectNameForFilter strips indentation") {
        db_ptr->setProjects({"Physics", "Quantum"});
        ALLOW_CALL(*db_ptr, GetProjectParent(std::string("Physics"))).RETURN(std::string{});
        ALLOW_CALL(*db_ptr, GetProjectParent(std::string("Quantum")))
            .RETURN(std::string("Physics"));

        Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

        auto options = core.GetFilterOptions();
        // Find the index of "  Quantum"
        auto it = std::find(options.begin(), options.end(), "  Quantum");
        REQUIRE(it != options.end());
        int idx = static_cast<int>(std::distance(options.begin(), it));
        REQUIRE(core.GetProjectNameForFilter(idx) == "Quantum");
    }

    SECTION("SetProjectParent delegates to database") {
        db_ptr->setProjects({"Physics", "Quantum"});

        REQUIRE_CALL(*db_ptr, SetProjectParent(std::string("Quantum"), std::string("Physics")));
        NAMED_ALLOW_CALL(*db_ptr, GetProjectParent(ANY(std::string))).RETURN(std::string{});

        Arxiv::AppCore core(config, std::move(db), std::move(fetcher));
        core.SetProjectParent("Quantum", "Physics");
    }
}

TEST_CASE("AppCore project notes", "[app][projects]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({"MyProject"});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("SetProjectNote delegates to database") {
        const std::string link = sample_articles[0].link;
        REQUIRE_CALL(*db_ptr,
                     SetProjectNote(std::string("MyProject"), link, std::string("my note")));

        core.SetProjectNote("MyProject", link, "my note");
    }

    SECTION("GetProjectNote delegates to database") {
        const std::string link = sample_articles[0].link;
        ALLOW_CALL(*db_ptr, GetProjectNote(std::string("MyProject"), link))
            .RETURN(std::string("stored note"));

        REQUIRE(core.GetProjectNote("MyProject", link) == "stored note");
    }
}

TEST_CASE("AppCore project export", "[app][projects]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({"ExportTest"});
    db_ptr->setProjectArticles("ExportTest", {sample_articles[0]});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("ExportProjectMarkdown creates a file") {
        const std::string path = "/tmp/test_export.md";
        bool ok = core.ExportProjectMarkdown("ExportTest", path);
        REQUIRE(ok);
        std::ifstream f(path);
        REQUIRE(f.good());
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        REQUIRE_THAT(content, ContainsSubstring("ExportTest"));
        REQUIRE_THAT(content, ContainsSubstring(sample_articles[0].title));
    }

    SECTION("ExportProjectText creates a file") {
        const std::string path = "/tmp/test_export.txt";
        bool ok = core.ExportProjectText("ExportTest", path);
        REQUIRE(ok);
        std::ifstream f(path);
        REQUIRE(f.good());
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        REQUIRE_THAT(content, ContainsSubstring(sample_articles[0].title));
    }

    SECTION("ExportProjectJSON creates valid JSON") {
        const std::string path = "/tmp/test_export.json";
        bool ok = core.ExportProjectJSON("ExportTest", path);
        REQUIRE(ok);
        std::ifstream f(path);
        REQUIRE(f.good());
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        REQUIRE_THAT(content, ContainsSubstring("\"name\""));
        REQUIRE_THAT(content, ContainsSubstring("ExportTest"));
        REQUIRE_THAT(content, ContainsSubstring("\"articles\""));
    }

    SECTION("ExportProjectMarkdown returns false for bad path") {
        bool ok = core.ExportProjectMarkdown("ExportTest", "/nonexistent/dir/out.md");
        REQUIRE_FALSE(ok);
    }
}

TEST_CASE("AppCore project import", "[app][projects]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({"ExportTest"});
    db_ptr->setProjectArticles("ExportTest", {sample_articles[0]});

    Arxiv::AppCore core_export(config, std::move(db), std::move(fetcher));

    // Export first, then import
    const std::string path = "/tmp/test_import_round_trip.json";
    REQUIRE(core_export.ExportProjectJSON("ExportTest", path));

    // Create a fresh AppCore for import
    auto db2 = std::make_unique<DatabaseManagerMock>();
    auto fetcher2 = std::make_unique<FetcherMock>();
    auto* db2_ptr = db2.get();

    db2_ptr->setArticles({});
    db2_ptr->setBookmarkedArticles({});
    db2_ptr->setProjects({});

    Arxiv::AppCore core_import(config, std::move(db2), std::move(fetcher2));

    SECTION("ImportProjectJSON round-trip succeeds") {
        bool ok = core_import.ImportProjectJSON(path);
        REQUIRE(ok);
    }

    SECTION("ImportProjectJSON returns false for missing file") {
        bool ok = core_import.ImportProjectJSON("/nonexistent/file.json");
        REQUIRE_FALSE(ok);
    }

    SECTION("ImportProjectJSON returns false for invalid JSON") {
        const std::string bad_path = "/tmp/test_bad.json";
        std::ofstream f(bad_path);
        f << "{ this is not valid json }";
        f.close();
        bool ok = core_import.ImportProjectJSON(bad_path);
        REQUIRE_FALSE(ok);
    }
}

// ---------------------------------------------------------------------------
// AppCore edge cases
// TDD: these tests were written first and confirmed failing before the fixes.
// ---------------------------------------------------------------------------
TEST_CASE("AppCore edge cases: SetFilterIndex bounds", "[app][edge]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({});
    ALLOW_CALL(*db_ptr, GetUnreadArticles()).RETURN(std::vector<Arxiv::Article>{});
    ALLOW_CALL(*db_ptr, GetTags()).RETURN(std::vector<std::string>{});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("SetFilterIndex with negative index clamps to 0") {
        // Out-of-range index must never reach the project branch of FetchArticles
        FORBID_CALL(*db_ptr, GetArticlesForProject(ANY(std::string)));
        core.SetFilterIndex(-1);
        REQUIRE(core.GetFilterIndex() == 0);
    }

    SECTION("SetFilterIndex beyond last valid index clamps to last valid index") {
        FORBID_CALL(*db_ptr, GetArticlesForProject(ANY(std::string)));
        int max_idx = static_cast<int>(core.GetFilterOptions().size()) - 1;
        core.SetFilterIndex(max_idx + 100);
        REQUIRE(core.GetFilterIndex() == max_idx);
    }
}

TEST_CASE("AppCore edge cases: AddProject validation", "[app][edge]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("AddProject with whitespace-only name is rejected") {
        FORBID_CALL(*db_ptr, AddProject(ANY(std::string)));
        core.AddProject("   ");
        core.AddProject("\t");
        core.AddProject("\n");
    }

    SECTION("AddProject with empty string is rejected") {
        FORBID_CALL(*db_ptr, AddProject(ANY(std::string)));
        core.AddProject("");
    }

    SECTION("AddProject with leading/trailing whitespace trims the name") {
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{"My Project"});
        ALLOW_CALL(*db_ptr, GetProjectParent(ANY(std::string))).RETURN(std::string{});
        REQUIRE_CALL(*db_ptr, AddProject(std::string("My Project")));
        core.AddProject("  My Project  ");
    }
}

TEST_CASE("AppCore edge cases: ToggleBookmark guard", "[app][edge]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("ToggleBookmark on article not in current list is a no-op") {
        FORBID_CALL(*db_ptr, ToggleBookmark(ANY(std::string), ANY(bool)));
        core.ToggleBookmark("https://arxiv.org/abs/nonexistent");
    }
}

TEST_CASE("AppCore edge cases: GetProjectNameForFilter", "[app][edge]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();

    auto* db_ptr = db.get();
    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("GetProjectNameForFilter with very large index returns empty") {
        REQUIRE(core.GetProjectNameForFilter(99999).empty());
    }

    SECTION("GetProjectNameForFilter with negative index returns empty") {
        REQUIRE(core.GetProjectNameForFilter(-1).empty());
    }
}

TEST_CASE("AppCore BibTeX generation", "[app][bibtex]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();
    auto* fetcher_ptr = fetcher.get();

    ALLOW_CALL(*fetcher_ptr, Fetch()).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, AddArticle(trompeloeil::_));
    ALLOW_CALL(*db_ptr, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, ListBookmarked()).RETURN(std::vector<Arxiv::Article>{});
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

    SECTION("Should use INSPIRE BibTeX when available") {
        std::string inspire_bibtex =
            "@article{Doe:2403.12345,\n    author = \"Doe, John\",\n    title = \"{Sample}\"\n}";
        ALLOW_CALL(*fetcher_ptr, FetchBibTeX(trompeloeil::_)).RETURN(inspire_bibtex);

        Arxiv::AppCore core(config, std::move(db), std::move(fetcher));
        auto articles = core.GetCurrentArticles();
        REQUIRE(!articles.empty());

        auto result = core.GetBibtex(articles[0]);
        REQUIRE(result == inspire_bibtex);
    }

    SECTION("Should fall back to constructed BibTeX when INSPIRE fails") {
        ALLOW_CALL(*fetcher_ptr, FetchBibTeX(trompeloeil::_)).RETURN(std::string(""));

        Arxiv::AppCore core(config, std::move(db), std::move(fetcher));
        auto articles = core.GetCurrentArticles();
        REQUIRE(!articles.empty());

        auto result = core.GetBibtex(articles[0]);
        REQUIRE_THAT(result, ContainsSubstring("@article{"));
        REQUIRE_THAT(result, ContainsSubstring("2403.12345"));
        REQUIRE_THAT(result, ContainsSubstring("Sample Article Title"));
        REQUIRE_THAT(result, ContainsSubstring("archivePrefix"));
    }
}

// ---------------------------------------------------------------------------
// Delete and bulk-action methods
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::DeleteCurrentOrSelected", "[app][delete]") {
    Config config("test/fixtures/test_config.yml");

    SECTION("Deletes the focused article when no selection is active") {
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

        std::string deleted_link;
        ALLOW_CALL(*db_ptr, DeleteArticle(ANY(std::string))).LR_SIDE_EFFECT(deleted_link = _1);

        AppCore core(config, std::move(db), std::move(fetcher));
        core.SetArticleIndex(0);
        core.DeleteCurrentOrSelected();

        REQUIRE(deleted_link == sample_articles[0].link);
    }

    SECTION("Deletes all selected articles when a selection is active") {
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

        std::vector<std::string> deleted;
        ALLOW_CALL(*db_ptr, DeleteArticle(ANY(std::string))).LR_SIDE_EFFECT(deleted.push_back(_1));

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        core.ToggleSelection(sample_articles[1].link);
        core.DeleteCurrentOrSelected();

        REQUIRE(deleted.size() == 2);
        REQUIRE(std::find(deleted.begin(), deleted.end(), sample_articles[0].link) !=
                deleted.end());
        REQUIRE(std::find(deleted.begin(), deleted.end(), sample_articles[1].link) !=
                deleted.end());
    }

    SECTION("Clears the selection after a bulk delete") {
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
        ALLOW_CALL(*db_ptr, DeleteArticle(ANY(std::string)));

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        core.DeleteCurrentOrSelected();

        REQUIRE(core.GetSelectionCount() == 0);
    }
}

TEST_CASE("AppCore::BookmarkSelected", "[app][bookmark]") {
    Config config("test/fixtures/test_config.yml");

    SECTION("Bookmarks all selected articles") {
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

        std::vector<std::pair<std::string, bool>> calls;
        ALLOW_CALL(*db_ptr, ToggleBookmark(ANY(std::string), ANY(bool)))
            .LR_SIDE_EFFECT(calls.push_back({_1, _2}));

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        core.ToggleSelection(sample_articles[1].link);
        core.BookmarkSelected(true);

        REQUIRE(calls.size() == 2);
        REQUIRE(std::all_of(calls.begin(), calls.end(), [](auto& p) { return p.second == true; }));
    }

    SECTION("Unbookmarks all selected articles") {
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

        std::vector<bool> states;
        ALLOW_CALL(*db_ptr, ToggleBookmark(ANY(std::string), ANY(bool)))
            .LR_SIDE_EFFECT(states.push_back(_2));

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        core.BookmarkSelected(false);

        REQUIRE(states.size() == 1);
        REQUIRE(states[0] == false);
    }
}

TEST_CASE("AppCore::AddSelectedToProject", "[app][projects]") {
    Config config("test/fixtures/test_config.yml");

    SECTION("Links all selected articles to the given project") {
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{"Proj"});

        std::vector<std::string> linked;
        ALLOW_CALL(*db_ptr, LinkArticleToProject(ANY(std::string), ANY(std::string)))
            .LR_SIDE_EFFECT(linked.push_back(_1));

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        core.ToggleSelection(sample_articles[1].link);
        core.AddSelectedToProject("Proj");

        REQUIRE(linked.size() == 2);
    }

    SECTION("Links current article when no selection active") {
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{"Proj"});

        std::string linked;
        ALLOW_CALL(*db_ptr, LinkArticleToProject(ANY(std::string), ANY(std::string)))
            .LR_SIDE_EFFECT(linked = _1);

        AppCore core(config, std::move(db), std::move(fetcher));
        core.SetArticleIndex(1);
        core.AddSelectedToProject("Proj");

        REQUIRE(linked == sample_articles[1].link);
    }
}

// ---------------------------------------------------------------------------
// AppCore export functions
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::ExportProjectMarkdown", "[app][export]") {
    auto tmp = (std::filesystem::temp_directory_path() / "arxiv_export_md_test.md").string();
    std::filesystem::remove(tmp);

    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{"TestProj"});
    ALLOW_CALL(*db_ptr, GetProjectParent(ANY(std::string))).RETURN("");
    ALLOW_CALL(*db_ptr, GetArticlesForProject("TestProj")).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjectNote(ANY(std::string), ANY(std::string))).RETURN("");

    AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Returns true and creates the file") {
        REQUIRE(core.ExportProjectMarkdown("TestProj", tmp));
        REQUIRE(std::filesystem::exists(tmp));
        std::filesystem::remove(tmp);
    }
}

TEST_CASE("AppCore::ExportProjectText", "[app][export]") {
    auto tmp = (std::filesystem::temp_directory_path() / "arxiv_export_txt_test.txt").string();
    std::filesystem::remove(tmp);

    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{"TestProj"});
    ALLOW_CALL(*db_ptr, GetProjectParent(ANY(std::string))).RETURN("");
    ALLOW_CALL(*db_ptr, GetArticlesForProject("TestProj")).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjectNote(ANY(std::string), ANY(std::string))).RETURN("");

    AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Returns true and creates the file") {
        REQUIRE(core.ExportProjectText("TestProj", tmp));
        REQUIRE(std::filesystem::exists(tmp));
        std::filesystem::remove(tmp);
    }
}

TEST_CASE("AppCore::ExportProjectJSON", "[app][export]") {
    auto tmp = (std::filesystem::temp_directory_path() / "arxiv_export_json_test.json").string();
    std::filesystem::remove(tmp);

    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{"TestProj"});
    ALLOW_CALL(*db_ptr, GetProjectParent(ANY(std::string))).RETURN("");
    ALLOW_CALL(*db_ptr, GetArticlesForProject("TestProj")).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjectNote(ANY(std::string), ANY(std::string))).RETURN("");

    AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Returns true and creates a JSON file") {
        REQUIRE(core.ExportProjectJSON("TestProj", tmp));
        REQUIRE(std::filesystem::exists(tmp));
        std::filesystem::remove(tmp);
    }
}

TEST_CASE("AppCore::ExportArticleBibTeX", "[app][export]") {
    auto tmp = (std::filesystem::temp_directory_path() / "arxiv_export_bib_test.bib").string();
    std::filesystem::remove(tmp);

    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
    ALLOW_CALL(*fetcher.get(), FetchBibTeX(ANY(std::string))).RETURN("");

    AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Returns true and writes fallback BibTeX") {
        REQUIRE(core.ExportArticleBibTeX(sample_articles[0], tmp));
        REQUIRE(std::filesystem::exists(tmp));
        std::filesystem::remove(tmp);
    }
}

TEST_CASE("AppCore::ExportSelectedDigest", "[app][export]") {
    SECTION("Returns empty string when nothing selected") {
        Config config("test/fixtures/test_config.yml");
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

        AppCore core(config, std::move(db), std::move(fetcher));
        REQUIRE(core.ExportSelectedDigest().empty());
    }

    SECTION("Returns path and creates digest file when articles are selected") {
        auto tmp_dir = std::filesystem::temp_directory_path() / "arxiv_digest_test";
        std::filesystem::remove_all(tmp_dir);

        Config config("test/fixtures/test_config.yml");
        config.set_download_dir(tmp_dir.string());

        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
        ALLOW_CALL(*fetcher.get(), DownloadPaper(ANY(std::string), ANY(std::string))).RETURN(false);

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        auto path = core.ExportSelectedDigest();

        REQUIRE_FALSE(path.empty());
        REQUIRE(std::filesystem::exists(path));

        std::filesystem::remove_all(tmp_dir);
    }

    SECTION("Returns empty string when selected links not in DB") {
        auto tmp_dir = std::filesystem::temp_directory_path() / "arxiv_digest_empty";
        std::filesystem::remove_all(tmp_dir);

        Config config("test/fixtures/test_config.yml");
        config.set_download_dir(tmp_dir.string());

        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(std::vector<Arxiv::Article>{});
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection("https://arxiv.org/abs/not.in.db");
        REQUIRE(core.ExportSelectedDigest().empty());

        std::filesystem::remove_all(tmp_dir);
    }
}

TEST_CASE("AppCore::ExportSelectedToObsidian", "[app][export]") {
    SECTION("Returns empty string when no vault configured") {
        Config config("test/fixtures/test_config.yml");
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        REQUIRE(core.ExportSelectedToObsidian().empty());
    }

    SECTION("Returns path and creates note when vault and selection are set") {
        auto vault = std::filesystem::temp_directory_path() / "arxiv_obsidian_vault";
        std::filesystem::remove_all(vault);

        Config config("test/fixtures/test_config.yml");
        config.set_obsidian_vault(vault.string());

        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
        ALLOW_CALL(*fetcher.get(), DownloadPaper(ANY(std::string), ANY(std::string))).RETURN(false);

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        auto path = core.ExportSelectedToObsidian();

        REQUIRE_FALSE(path.empty());
        REQUIRE(std::filesystem::exists(path));

        std::filesystem::remove_all(vault);
    }

    SECTION("Returns empty string when no selections") {
        Config config("test/fixtures/test_config.yml");
        config.set_obsidian_vault("/tmp/some_vault");
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

        AppCore core(config, std::move(db), std::move(fetcher));
        REQUIRE(core.ExportSelectedToObsidian().empty());
    }
}

// ---------------------------------------------------------------------------
// v0.8: read/unread, pruning
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::MarkArticleRead", "[app][read]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

    SECTION("Delegates to DatabaseManager::MarkArticleRead") {
        std::string marked;
        ALLOW_CALL(*db_ptr, MarkArticleRead(ANY(std::string))).LR_SIDE_EFFECT(marked = _1);

        AppCore core(config, std::move(db), std::move(fetcher));
        core.MarkArticleRead(sample_articles[0].link);
        REQUIRE(marked == sample_articles[0].link);
    }
}

TEST_CASE("AppCore Unread filter", "[app][filter]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    std::vector<Article> unread_articles = {sample_articles[1]};

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
    ALLOW_CALL(*db_ptr, GetUnreadArticles()).RETURN(unread_articles);

    SECTION("Unread filter shows only unread articles") {
        AppCore core(config, std::move(db), std::move(fetcher));
        core.SetFilterIndex(AppCore::FilterView::Unread);
        auto articles = core.GetCurrentArticles();
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].link == sample_articles[1].link);
    }
}

TEST_CASE("AppCore startup pruning", "[app][prune]") {
    SECTION("Calls PruneArticles with configured max_article_age_days") {
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

        int pruned_days = -1;
        ALLOW_CALL(*db_ptr, PruneArticles(ANY(int))).LR_SIDE_EFFECT(pruned_days = _1);

        Config cfg("test/fixtures/test_config.yml");
        cfg.set_max_article_age_days(45);
        AppCore core(cfg, std::move(db), std::move(fetcher));
        REQUIRE(pruned_days == 45);
    }

    SECTION("Does not call PruneArticles when max_article_age_days is 0") {
        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        Config config("test/fixtures/test_config.yml");
        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

        bool called = false;
        ALLOW_CALL(*db_ptr, PruneArticles(ANY(int))).LR_SIDE_EFFECT(called = true);

        AppCore core(config, std::move(db), std::move(fetcher));
        REQUIRE_FALSE(called);
    }
}

// Note: Most of the ArxivApp's functionality is UI-based and involves
// private components that can't be directly tested in unit tests.
// The actual functionality should be tested through integration tests
// that simulate user interactions with the UI.
