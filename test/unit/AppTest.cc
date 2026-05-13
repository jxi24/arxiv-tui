#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <Arxiv/AppCore.hh>
#include <Arxiv/Config.hh>
#include <fixtures/test_data.hh>
#include <mocks/DatabaseManagerMock.hh>
#include <mocks/FetcherMock.hh>
#include <fstream>
#include <cstdio>
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
        ALLOW_CALL(*db_ptr, GetRecent(ANY(int)))
            .RETURN(sample_articles);

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
    
    // Set up mock responses
    fetcher_ptr->setFetchResponse(sample_articles);
    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({}); // Empty bookmarked articles initially
    db_ptr->setProjects({}); // Empty projects initially
    
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
        REQUIRE(filtered_today.size() == today_articles.size());
    }
    
    SECTION("Should handle article bookmarking") {
        auto articles = core.GetCurrentArticles();
        if(!articles.empty()) {
            std::string article_link = articles[0].link;
            bool initial_state = core.IsArticleBookmarked(article_link);
            
            // Set up mock response for bookmark toggle
            db_ptr->setBookmarkedArticles({articles[0]});
            
            core.ToggleBookmark(article_link);
            REQUIRE(core.IsArticleBookmarked(article_link) != initial_state);
            
            // Set up mock response for unbookmark
            db_ptr->setBookmarkedArticles({});
            
            core.ToggleBookmark(article_link);
            REQUIRE(core.IsArticleBookmarked(article_link) == initial_state);
        }
    }
}

TEST_CASE("AppCore project management", "[app]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();
    auto* fetcher_ptr = fetcher.get();
    
    // Set up mock responses
    fetcher_ptr->setFetchResponse(sample_articles);
    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({}); // Empty bookmarked articles initially
    db_ptr->setProjects({}); // Empty projects initially
    
    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));
    
    SECTION("Should handle project creation and removal") {
        std::string project_name = "Test Project";
        auto initial_projects = core.GetProjects();
        
        // Set up mock response for project creation
        db_ptr->setProjects({project_name});
        
        core.AddProject(project_name);
        auto projects = core.GetProjects();
        REQUIRE(projects.size() == initial_projects.size() + 1);
        REQUIRE(std::find(projects.begin(), projects.end(), project_name) != projects.end());
        
        // Set up mock response for project removal
        db_ptr->setProjects({});
        
        core.RemoveProject(project_name);
        projects = core.GetProjects();
        REQUIRE(projects.size() == initial_projects.size());
        REQUIRE(std::find(projects.begin(), projects.end(), project_name) == projects.end());
    }
    
    SECTION("Should handle article-project linking") {
        auto articles = core.GetCurrentArticles();
        if(!articles.empty()) {
            std::string article_link = articles[0].link;
            std::string project_name = "Test Project";
            
            // Set up mock responses for project operations
            db_ptr->setProjects({project_name});
            db_ptr->setProjectArticles(project_name, {articles[0]});
            
            core.AddProject(project_name);
            core.LinkArticleToProject(article_link, project_name);
            
            auto project_articles = core.GetArticlesForProject(project_name);
            REQUIRE(std::find_if(project_articles.begin(), project_articles.end(),
                [&](const Arxiv::Article& a) { return a.link == article_link; }) != project_articles.end());
            
            // Set up mock response for unlinking
            db_ptr->setProjectArticles(project_name, {});
            
            core.UnlinkArticleFromProject(article_link, project_name);
            project_articles = core.GetArticlesForProject(project_name);
            REQUIRE(std::find_if(project_articles.begin(), project_articles.end(),
                [&](const Arxiv::Article& a) { return a.link == article_link; }) == project_articles.end());
        }
    }
}

TEST_CASE("AppCore state management", "[app]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();
    auto* fetcher_ptr = fetcher.get();
    
    // Set up mock responses
    fetcher_ptr->setFetchResponse(sample_articles);
    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({}); // Empty bookmarked articles initially
    db_ptr->setProjects({}); // Empty projects initially
    
    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));
    
    SECTION("Should handle article index changes") {
        auto articles = core.GetCurrentArticles();
        if(!articles.empty()) {
            int initial_index = core.GetArticleIndex();
            core.SetArticleIndex(initial_index + 1);
            REQUIRE(core.GetArticleIndex() == initial_index + 1);
        }
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
    config.set_ranker_file("/tmp/arxiv_tui_apptest_ranker_" +
                           std::to_string(::getpid()) + ".bin");
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
        ALLOW_CALL(*db_ptr, GetProjectParent(std::string("Physics")))
            .RETURN(std::string{});
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
        ALLOW_CALL(*db_ptr, GetProjectParent(std::string("Physics")))
            .RETURN(std::string{});
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
        NAMED_ALLOW_CALL(*db_ptr, GetProjectParent(ANY(std::string)))
            .RETURN(std::string{});

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
        REQUIRE_CALL(*db_ptr, SetProjectNote(
            std::string("MyProject"), link, std::string("my note")));

        core.SetProjectNote("MyProject", link, "my note");
    }

    SECTION("GetProjectNote delegates to database") {
        const std::string link = sample_articles[0].link;
        ALLOW_CALL(*db_ptr, GetProjectNote(
            std::string("MyProject"), link))
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
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        REQUIRE_THAT(content, ContainsSubstring("ExportTest"));
        REQUIRE_THAT(content, ContainsSubstring(sample_articles[0].title));
    }

    SECTION("ExportProjectText creates a file") {
        const std::string path = "/tmp/test_export.txt";
        bool ok = core.ExportProjectText("ExportTest", path);
        REQUIRE(ok);
        std::ifstream f(path);
        REQUIRE(f.good());
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        REQUIRE_THAT(content, ContainsSubstring(sample_articles[0].title));
    }

    SECTION("ExportProjectJSON creates valid JSON") {
        const std::string path = "/tmp/test_export.json";
        bool ok = core.ExportProjectJSON("ExportTest", path);
        REQUIRE(ok);
        std::ifstream f(path);
        REQUIRE(f.good());
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
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

// Note: Most of the ArxivApp's functionality is UI-based and involves
// private components that can't be directly tested in unit tests.
// The actual functionality should be tested through integration tests
// that simulate user interactions with the UI.
