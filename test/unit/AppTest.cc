// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <Arxiv/AppCore.hh>
#include <Arxiv/Config.hh> // also yaml-cpp, with its warnings silenced
#include <Arxiv/DatabaseManager.hh>
#include <Arxiv/Export.hh>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdio>
#include <filesystem>
#include <fixtures/TimeZone.hh>
#include <fixtures/test_data.hh>
#include <fstream>
#include <mocks/DatabaseManagerMock.hh>
#include <mocks/FetcherMock.hh>
#include <sstream>
#include <thread>
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

TEST_CASE("Project import does not overwrite articles already in the database",
          "[app][projects][regression]") {
    // An exported project carries title/authors/abstract/date only, and an
    // import file may be old or hand-edited: the stored paper (category,
    // replacement flag, current title) wins.
    auto dir = std::filesystem::temp_directory_path() /
               ("arxiv_import_keep_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    auto stored = sample_articles[0];
    stored.category = "hep-ph";
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    db->AddArticle(stored);
    auto* db_ptr = db.get();
    Config config("test/fixtures/test_config.yml");
    AppCore core(
        config, std::move(db), std::make_unique<FetcherMock>(), AppCore::FetchMode::Offline);

    auto file = dir / "p.json";
    std::ofstream(file) << R"({"name":"P","articles":[{"link":")" << stored.link
                        << R"(","title":"Edited title","authors":"","abstract":""}]})";
    REQUIRE(core.ImportProjectJSON(file.string()));
    auto after = db_ptr->GetArticle(stored.link);

    SECTION("title kept") { REQUIRE(after->title == stored.title); }
    SECTION("authors kept") { REQUIRE(after->authors == stored.authors); }
    SECTION("category kept") { REQUIRE(after->category == "hep-ph"); }
    SECTION("still linked to the project") {
        auto projects = db_ptr->GetProjectsForArticle(stored.link);
        REQUIRE(std::find(projects.begin(), projects.end(), "P") != projects.end());
    }
    std::filesystem::remove_all(dir);
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

    SECTION("ImportProjectJSON refuses a file over the size limit") {
        // Valid JSON padded with whitespace: only the size check can stop it
        // being read whole into memory.
        const std::string big_path = "/tmp/test_import_too_big.json";
        {
            std::ofstream f(big_path);
            f << R"({"name":"Huge","articles":[]})";
            f << std::string(Arxiv::AppCore::kMaxImportBytes, ' ');
        }
        bool ok = core_import.ImportProjectJSON(big_path);
        std::filesystem::remove(big_path);
        REQUIRE_FALSE(ok);
    }

    SECTION("ImportProjectJSON returns false for a number JSON cannot hold") {
        // Found by fuzz_import_json: nlohmann throws out_of_range, not
        // parse_error, for 1e70000.
        const std::string huge_path = "/tmp/test_import_huge_number.json";
        {
            std::ofstream f(huge_path);
            f << R"({"name":"p","date":1e70000})";
        }
        bool ok = true;
        REQUIRE_NOTHROW(ok = core_import.ImportProjectJSON(huge_path));
        std::filesystem::remove(huge_path);
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

namespace {
// AppCore over the sample articles exporting to `vault` (PDFs linked only).
std::unique_ptr<AppCore> obsidian_core(const std::filesystem::path& vault) {
    Config config("test/fixtures/test_config.yml");
    config.set_obsidian_vault(vault.string());
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    for (const auto& a : sample_articles)
        db->AddArticle(a);
    auto core = std::make_unique<AppCore>(
        config, std::move(db), std::make_unique<FetcherMock>(), AppCore::FetchMode::Offline);
    core->SetFilterIndex(AppCore::FilterView::All);
    return core;
}

std::string read_all(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("Obsidian export keeps reading notes written in an existing note",
          "[app][export][obsidian][regression]") {
    auto vault = std::filesystem::temp_directory_path() /
                 ("arxiv_obsidian_keep_" + std::to_string(::getpid()));
    std::filesystem::remove_all(vault);
    auto core = obsidian_core(vault);
    core->ToggleSelection(sample_articles[0].link);
    auto index = core->ExportSelectedToObsidian();
    REQUIRE_FALSE(index.empty());
    auto note =
        std::filesystem::path(index).parent_path() / (sample_articles[0].file_stem() + ".md");
    {
        std::ofstream(note, std::ios::app) << "My thoughts on this paper.\n";
    }

    REQUIRE_FALSE(core->ExportSelectedToObsidian().empty());

    REQUIRE_THAT(read_all(note), ContainsSubstring("My thoughts on this paper."));
    std::filesystem::remove_all(vault);
}

TEST_CASE("Obsidian export on the same day keeps earlier papers in the index",
          "[app][export][obsidian][regression]") {
    auto vault = std::filesystem::temp_directory_path() /
                 ("arxiv_obsidian_index_" + std::to_string(::getpid()));
    std::filesystem::remove_all(vault);
    auto core = obsidian_core(vault);

    core->ToggleSelection(sample_articles[0].link);
    REQUIRE_FALSE(core->ExportSelectedToObsidian().empty());
    core->ClearSelections();
    core->ToggleSelection(sample_articles[1].link);
    auto index = core->ExportSelectedToObsidian();
    REQUIRE_FALSE(index.empty());

    auto content = read_all(index);
    SECTION("first batch still linked") {
        REQUIRE_THAT(content, ContainsSubstring("[[" + sample_articles[0].file_stem() + "|"));
    }
    SECTION("second batch linked") {
        REQUIRE_THAT(content, ContainsSubstring("[[" + sample_articles[1].file_stem() + "|"));
    }
    SECTION("re-exporting a paper does not list it twice") {
        core->ClearSelections();
        core->ToggleSelection(sample_articles[1].link);
        content = read_all(core->ExportSelectedToObsidian());
        const std::string link = "[[" + sample_articles[1].file_stem() + "|";
        REQUIRE(content.find(link) == content.rfind(link));
    }
    std::filesystem::remove_all(vault);
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

    SECTION("Links to the PDF on arXiv and downloads nothing by default") {
        auto vault = std::filesystem::temp_directory_path() / "arxiv_obsidian_link_only";
        std::filesystem::remove_all(vault);

        Config config("test/fixtures/test_config.yml");
        config.set_obsidian_vault(vault.string());
        REQUIRE_FALSE(config.get_obsidian_download_pdfs());

        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
        FORBID_CALL(*fetcher.get(), DownloadPaper(ANY(std::string), ANY(std::string)));

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        auto path = core.ExportSelectedToObsidian();
        REQUIRE_FALSE(path.empty());

        const std::string id = sample_articles[0].id();
        std::ifstream note(std::filesystem::path(path).parent_path() / (id + ".md"));
        std::stringstream content;
        content << note.rdbuf();
        REQUIRE_THAT(content.str(),
                     ContainsSubstring("[PDF on arXiv](https://arxiv.org/pdf/" + id + ")"));
        REQUIRE_THAT(content.str(), !ContainsSubstring("![[" + id + ".pdf]]"));
        REQUIRE_FALSE(
            std::filesystem::exists(std::filesystem::path(path).parent_path() / (id + ".pdf")));

        std::filesystem::remove_all(vault);
    }

    SECTION("Embeds and downloads the PDF when obsidian_download_pdfs is on") {
        auto vault = std::filesystem::temp_directory_path() / "arxiv_obsidian_download";
        std::filesystem::remove_all(vault);

        Config config("test/fixtures/test_config.yml");
        config.set_obsidian_vault(vault.string());
        config.set_obsidian_download_pdfs(true);

        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
        REQUIRE_CALL(*fetcher.get(), DownloadPaper(ANY(std::string), ANY(std::string)))
            .RETURN(true);

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        auto path = core.ExportSelectedToObsidian();
        REQUIRE_FALSE(path.empty());

        const std::string id = sample_articles[0].id();
        std::ifstream note(std::filesystem::path(path).parent_path() / (id + ".md"));
        std::stringstream content;
        content << note.rdbuf();
        REQUIRE_THAT(content.str(), ContainsSubstring("![[" + id + ".pdf]]"));

        std::filesystem::remove_all(vault);
    }

    SECTION("Still writes the note when the PDF download fails") {
        auto vault = std::filesystem::temp_directory_path() / "arxiv_obsidian_download_fail";
        std::filesystem::remove_all(vault);

        Config config("test/fixtures/test_config.yml");
        config.set_obsidian_vault(vault.string());
        config.set_obsidian_download_pdfs(true);

        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
        REQUIRE_CALL(*fetcher.get(), DownloadPaper(ANY(std::string), ANY(std::string)))
            .RETURN(false);

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(sample_articles[0].link);
        auto path = core.ExportSelectedToObsidian();
        REQUIRE_FALSE(path.empty());

        const std::string id = sample_articles[0].id();
        REQUIRE(std::filesystem::exists(std::filesystem::path(path).parent_path() / (id + ".md")));

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

    SECTION("Escapes special characters in YAML frontmatter") {
        // Exercises yaml_quote (backslash, quote, newline branches) and
        // yaml_inline_list (multi-category comma path).
        auto vault = std::filesystem::temp_directory_path() / "arxiv_obsidian_special";
        std::filesystem::remove_all(vault);

        Config config("test/fixtures/test_config.yml");
        config.set_obsidian_vault(vault.string());

        Arxiv::Article special = sample_articles[0];
        special.title = "A \"quoted\" title with \\backslash\nand newline";
        special.category = "hep-ph, hep-lat,";

        auto db = std::make_unique<DatabaseManagerMock>();
        auto fetcher = std::make_unique<FetcherMock>();
        auto* db_ptr = db.get();

        ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(std::vector<Arxiv::Article>{special});
        ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
        ALLOW_CALL(*fetcher.get(), DownloadPaper(ANY(std::string), ANY(std::string))).RETURN(false);

        AppCore core(config, std::move(db), std::move(fetcher));
        core.ToggleSelection(special.link);
        auto path = core.ExportSelectedToObsidian();

        REQUIRE_FALSE(path.empty());
        std::filesystem::remove_all(vault);
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

TEST_CASE("AppCore::RateSelected triggers retrain at threshold", "[app][ranking]") {
    Config config("test/fixtures/test_config.yml");
    config.set_retrain_interval(2);
    config.set_ranker_file("/tmp/arxiv_tui_ratesel_retrain_" + std::to_string(::getpid()) + ".bin");
    std::remove(config.get_ranker_file().c_str());

    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    db_ptr->setArticles(sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Reaches retrain threshold when selection covers enough articles") {
        auto articles = core.GetCurrentArticles();
        REQUIRE(articles.size() >= 2);

        core.ToggleSelection(articles[0].link);
        core.ToggleSelection(articles[1].link);

        // Two articles × rating = 2 ratings → reaches retrain_interval of 2.
        Arxiv::DatabaseManager::RatedArticleList rated = {{articles[0], 4}, {articles[1], 4}};
        ALLOW_CALL(*db_ptr, SetRating(ANY(std::string), ANY(int)));
        ALLOW_CALL(*db_ptr, GetRatedArticles()).RETURN(rated);
        ALLOW_CALL(*db_ptr, GetRecent(-1)).RETURN(sample_articles);

        REQUIRE_NOTHROW(core.RateSelected(4));
    }
}

TEST_CASE("AppCore::ToggleSelection deselect and ClearSelections", "[app][selection]") {
    Config config("test/fixtures/test_config.yml");

    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));
    const std::string& link = sample_articles[0].link;

    SECTION("ToggleSelection removes an already-selected article") {
        core.ToggleSelection(link);
        REQUIRE(core.IsSelected(link));
        core.ToggleSelection(link); // deselect
        REQUIRE_FALSE(core.IsSelected(link));
        REQUIRE(core.GetSelectionCount() == 0);
    }

    SECTION("ClearSelections removes all selected articles") {
        core.ToggleSelection(sample_articles[0].link);
        core.ToggleSelection(sample_articles[1].link);
        REQUIRE(core.GetSelectionCount() == 2);
        core.ClearSelections();
        REQUIRE(core.GetSelectionCount() == 0);
    }
}

TEST_CASE("AppCore::BookmarkSelected falls back to focused article when no selection",
          "[app][bookmark]") {
    Config config("test/fixtures/test_config.yml");

    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

    std::vector<std::string> bookmarked_links;
    ALLOW_CALL(*db_ptr, ToggleBookmark(ANY(std::string), ANY(bool)))
        .LR_SIDE_EFFECT(bookmarked_links.push_back(_1));

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Bookmarks focused article when no selection is active") {
        REQUIRE(core.GetSelectionCount() == 0);
        core.BookmarkSelected(true);
        REQUIRE(bookmarked_links.size() == 1);
        REQUIRE(bookmarked_links[0] == sample_articles[0].link);
    }
}

TEST_CASE("AppCore inline accessor coverage", "[app][accessors]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

    AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("IsTraining returns false initially") { REQUIRE_FALSE(core.IsTraining()); }

    SECTION("PendingRatings returns 0 initially") { REQUIRE(core.PendingRatings() == 0); }

    SECTION("GetDateRange and ClearDateRange round-trip") {
        core.SetDateRange("2026-01-01", "2026-06-01");
        auto [s, e] = core.GetDateRange();
        REQUIRE(s == "2026-01-01");
        REQUIRE(e == "2026-06-01");
        core.ClearDateRange();
        auto [s2, e2] = core.GetDateRange();
        REQUIRE(s2.empty());
        REQUIRE(e2.empty());
    }

    SECTION("GetSearchQuery and ClearSearch round-trip") {
        ALLOW_CALL(*db_ptr, SearchArticles(ANY(std::string), ANY(bool), ANY(bool), ANY(bool)))
            .RETURN(sample_articles);
        core.SetSearchQuery("Higgs");
        REQUIRE(core.GetSearchQuery() == "Higgs");
        core.ClearSearch();
        REQUIRE(core.GetSearchQuery().empty());
    }

    SECTION("IsCategoryActive reflects configured topics") {
        auto topics = config.get_topics();
        REQUIRE_FALSE(topics.empty());
        REQUIRE(core.IsCategoryActive(topics[0]));
    }
}

// Note: Most of the ArxivApp's functionality is UI-based and involves
// private components that can't be directly tested in unit tests.
// The actual functionality should be tested through integration tests
// that simulate user interactions with the UI.

TEST_CASE("AppCore::ToggleCategory", "[app][category]") {
    // Use an in-memory config with explicit topics so the test is not
    // sensitive to which YAML file is on disk.
    Config config("test/fixtures/test_config.yml");
    config.set_topics({"hep-ph", "hep-ex", "hep-lat"});

    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();

    std::vector<Article> articles = {
        {"PH Article",
         "https://arxiv.org/abs/1",
         "abstract",
         "Author A",
         std::chrono::system_clock::now(),
         "hep-ph",
         false},
        {"EX Article",
         "https://arxiv.org/abs/2",
         "abstract",
         "Author B",
         std::chrono::system_clock::now(),
         "hep-ex",
         false},
        {"LAT Article",
         "https://arxiv.org/abs/3",
         "abstract",
         "Author C",
         std::chrono::system_clock::now(),
         "hep-lat",
         false},
    };
    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});

    AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("all topics active by default") {
        REQUIRE(core.IsCategoryActive("hep-ph"));
        REQUIRE(core.IsCategoryActive("hep-ex"));
        REQUIRE(core.IsCategoryActive("hep-lat"));
    }

    SECTION("ToggleCategory deactivates an active topic") {
        core.ToggleCategory("hep-ph");
        REQUIRE_FALSE(core.IsCategoryActive("hep-ph"));
        REQUIRE(core.IsCategoryActive("hep-ex"));
    }

    SECTION("ToggleCategory reactivates a deactivated topic") {
        core.ToggleCategory("hep-ph");
        core.ToggleCategory("hep-ph");
        REQUIRE(core.IsCategoryActive("hep-ph"));
    }

    SECTION("ToggleCategory filters GetCurrentArticles") {
        core.ToggleCategory("hep-ph");
        auto visible = core.GetCurrentArticles();
        for (const auto& a : visible) {
            REQUIRE(a.category != "hep-ph");
        }
    }

    SECTION("SetActiveCategories with empty set hides all categorised articles") {
        core.SetActiveCategories({});
        // All articles have a non-empty category, so none should be visible.
        REQUIRE(core.GetCurrentArticles().empty());
    }

    SECTION("SetActiveCategories with subset shows only matching articles") {
        core.SetActiveCategories({"hep-ex"});
        auto visible = core.GetCurrentArticles();
        REQUIRE(visible.size() == 1);
        REQUIRE(visible[0].category == "hep-ex");
    }

    SECTION("articles with empty category always pass through") {
        Article no_cat{"No-Cat Article",
                       "https://arxiv.org/abs/4",
                       "abstract",
                       "Author D",
                       std::chrono::system_clock::now(),
                       "",
                       false};
        std::vector<Article> mixed = articles;
        mixed.push_back(no_cat);

        auto db2 = std::make_unique<DatabaseManagerMock>();
        auto* db2_ptr = db2.get();
        ALLOW_CALL(*db2_ptr, GetRecent(ANY(int))).RETURN(mixed);
        ALLOW_CALL(*db2_ptr, GetProjects()).RETURN(std::vector<std::string>{});
        AppCore core2(config, std::move(db2), std::make_unique<FetcherMock>());

        core2.SetActiveCategories({});
        auto visible = core2.GetCurrentArticles();
        REQUIRE(visible.size() == 1);
        REQUIRE(visible[0].category.empty());
    }
}

// ---------------------------------------------------------------------------
// AppCore::GetLinksToOpen
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::GetLinksToOpen returns browser targets", "[app][browser]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto* db_ptr = db.get();
    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
    AppCore core(config, std::move(db), std::make_unique<FetcherMock>());

    SECTION("returns focused article link when no selection") {
        core.SetArticleIndex(0);
        auto links = core.GetLinksToOpen();
        REQUIRE(links.size() == 1);
        REQUIRE(links[0] == sample_articles[0].link);
    }

    SECTION("returns second article when focused on index 1") {
        core.SetArticleIndex(1);
        auto links = core.GetLinksToOpen();
        REQUIRE(links.size() == 1);
        REQUIRE(links[0] == sample_articles[1].link);
    }

    SECTION("returns all selected links when selection is active, ignoring focus") {
        core.SetArticleIndex(0);
        core.ToggleSelection(sample_articles[0].link);
        core.ToggleSelection(sample_articles[1].link);
        auto links = core.GetLinksToOpen();
        REQUIRE(links.size() == 2);
        REQUIRE(std::find(links.begin(), links.end(), sample_articles[0].link) != links.end());
        REQUIRE(std::find(links.begin(), links.end(), sample_articles[1].link) != links.end());
    }

    SECTION("returns empty when article list is empty") {
        auto db2 = std::make_unique<DatabaseManagerMock>();
        auto* db2_ptr = db2.get();
        ALLOW_CALL(*db2_ptr, GetRecent(ANY(int))).RETURN(std::vector<Article>{});
        ALLOW_CALL(*db2_ptr, GetProjects()).RETURN(std::vector<std::string>{});
        AppCore empty_core(config, std::move(db2), std::make_unique<FetcherMock>());
        REQUIRE(empty_core.GetLinksToOpen().empty());
    }
}

// ---------------------------------------------------------------------------
// Regressions found in code review — each fails until the bug is fixed.
// These run AppCore against a real in-memory database so they exercise the
// SQL, not a mock's idea of it.
// ---------------------------------------------------------------------------
namespace {

Config regression_config() {
    static int counter = 0;
    auto tmp = std::filesystem::temp_directory_path();
    Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir(tmp.string());
    // Unique per core: a model saved by one test must not leak into another.
    auto ranker = tmp / ("arxiv_app_regression_" + std::to_string(::getpid()) + "_" +
                         std::to_string(counter++) + ".bin");
    std::filesystem::remove(ranker);
    cfg.set_ranker_file(ranker.string());
    return cfg;
}

std::unique_ptr<AppCore> make_memory_core(const std::vector<Article>& articles = {},
                                          Config cfg = regression_config()) {
    auto db = std::make_unique<DatabaseManager>(":memory:");
    for (const auto& a : articles)
        db->AddArticle(a);
    return std::make_unique<AppCore>(cfg, std::move(db), std::make_unique<FetcherMock>());
}

Article make_article(const std::string& id, const std::string& authors, const std::string& cat) {
    return Article("Title " + id,
                   "https://arxiv.org/abs/" + id,
                   "Abstract " + id,
                   authors,
                   std::chrono::system_clock::now(),
                   cat);
}

std::string write_temp_json(const std::string& name, const std::string& body) {
    auto path = (std::filesystem::temp_directory_path() /
                 ("arxiv_app_regression_" + std::to_string(::getpid()) + "_" + name + ".json"))
                    .string();
    std::ofstream(path) << body;
    return path;
}

bool has_link(const std::vector<Article>& articles, const std::string& link) {
    return std::any_of(
        articles.begin(), articles.end(), [&](const Article& a) { return a.link == link; });
}

} // namespace

TEST_CASE("AppCore regression: ImportProjectJSON survives wrongly typed fields",
          "[app][import][regression]") {
    auto core = make_memory_core();
    auto path = write_temp_json("bad_types",
                                R"({"name": "p", "articles": [
                                      1, "x", null,
                                      {"link": "https://arxiv.org/abs/2401.00001", "date": "2024-01-01"},
                                      {"link": "https://arxiv.org/abs/2401.00002", "title": 7},
                                      {"link": "https://arxiv.org/abs/2401.00003"}
                                    ]})");

    REQUIRE_NOTHROW(core->ImportProjectJSON(path));
    REQUIRE(has_link(core->GetArticlesForProject("p"), "https://arxiv.org/abs/2401.00003"));
    std::filesystem::remove(path);
}

TEST_CASE("AppCore regression: ImportProjectJSON rejects links that are not arXiv abstract URLs",
          "[app][import][regression]") {
    // Links reach xdg-open through a shell and become file names on export.
    auto core = make_memory_core();
    auto path = write_temp_json("bad_links",
                                R"json({"name": "p", "articles": [
                                      {"link": "https://arxiv.org/abs/2401.1\"; touch /tmp/pwned; \""},
                                      {"link": "javascript:alert(1)"},
                                      {"link": "https://arxiv.org/abs/2401.00003"}
                                    ]})json");

    REQUIRE(core->ImportProjectJSON(path));
    auto imported = core->GetArticlesForProject("p");
    REQUIRE(imported.size() == 1);
    REQUIRE(imported[0].link == "https://arxiv.org/abs/2401.00003");
    std::filesystem::remove(path);
}

TEST_CASE("AppCore regression: importing into an existing sub-project keeps it nested",
          "[app][import][regression]") {
    auto core = make_memory_core();
    core->AddProject("parent");
    core->AddProject("child");
    core->SetProjectParent("child", "parent");
    auto path = write_temp_json("nested", R"({"name": "child", "articles": []})");

    REQUIRE(core->ImportProjectJSON(path));

    auto opts = core->GetFilterOptions();
    REQUIRE(std::find(opts.begin(), opts.end(), "  child") != opts.end());
    std::filesystem::remove(path);
}

TEST_CASE("AppCore regression: sub-projects deeper than one level appear in the filter list",
          "[app][project][regression]") {
    auto core = make_memory_core();
    core->AddProject("A");
    core->AddProject("B");
    core->AddProject("C");
    core->SetProjectParent("B", "A");
    core->SetProjectParent("C", "B");

    auto opts = core->GetFilterOptions();
    bool has_c = std::any_of(opts.begin(), opts.end(), [](const std::string& o) {
        return o.find_first_not_of(' ') != std::string::npos &&
               o.substr(o.find_first_not_of(' ')) == "C";
    });
    REQUIRE(has_c);
}

TEST_CASE("AppCore regression: a project whose parent no longer exists stays visible",
          "[app][project][regression]") {
    auto core = make_memory_core();
    core->AddProject("orphan");
    core->SetProjectParent("orphan", "missing");

    auto opts = core->GetFilterOptions();
    bool has_orphan = std::any_of(opts.begin(), opts.end(), [](const std::string& o) {
        return o.find("orphan") != std::string::npos;
    });
    REQUIRE(has_orphan);
}

TEST_CASE("AppCore regression: the category filter matches whole category names",
          "[app][category][regression]") {
    Config cfg = regression_config();
    cfg.set_topics({"math", "math-ph"});
    auto mathph = make_article("2401.00010", "A", "math-ph");
    auto mathag = make_article("2401.00011", "B", "math.AG");
    auto core = make_memory_core({mathph, mathag}, cfg);
    core->SetFilterIndex(AppCore::FilterView::All);

    core->ToggleCategory("math-ph"); // only "math" stays active

    // "math" is a substring of "math-ph", but math-ph is a different archive.
    REQUIRE_FALSE(has_link(core->GetCurrentArticles(), mathph.link));
}

TEST_CASE("AppCore regression: SetArticleIndex keeps the index inside the article list",
          "[app][index][regression]") {
    SECTION("past the end clamps to the last article") {
        auto core = make_memory_core(
            {make_article("2401.00020", "A", "hep-ph"), make_article("2401.00021", "B", "hep-ph")});
        core->SetFilterIndex(AppCore::FilterView::All);
        core->SetArticleIndex(99);
        REQUIRE(core->GetArticleIndex() == 1);
    }
    SECTION("negative clamps to zero") {
        auto core = make_memory_core({make_article("2401.00022", "A", "hep-ph")});
        core->SetFilterIndex(AppCore::FilterView::All);
        core->SetArticleIndex(-1);
        REQUIRE(core->GetArticleIndex() == 0);
    }
    SECTION("empty list stays at zero") {
        auto core = make_memory_core();
        core->SetArticleIndex(5);
        REQUIRE(core->GetArticleIndex() == 0);
    }
}

TEST_CASE("AppCore regression: following an author does not match longer names",
          "[app][author][regression]") {
    auto lin = make_article("2401.00030", "Wei Lin, X. Other", "hep-ph");
    auto li = make_article("2401.00031", "Y. Other, Wei Li", "hep-ph");
    auto core = make_memory_core({lin, li});

    core->FollowAuthor("Wei Li");
    auto followed = core->GetArticlesForFollowedAuthors();

    REQUIRE_FALSE(has_link(followed, lin.link));
}

TEST_CASE("AppCore regression: Recommended view does not race the training thread",
          "[app][threads][regression]") {
    // Only observable under ThreadSanitizer (the Sanitizers CI workflow):
    // FetchArticles scores the Recommended view through m_ranker without
    // m_ranker_mutex while the training thread move-assigns a new model.
    std::vector<Article> articles;
    for (int i = 0; i < 6; ++i)
        articles.push_back(make_article("2401.0004" + std::to_string(i), "A", "hep-ph"));
    auto db = std::make_unique<DatabaseManager>(":memory:");
    for (std::size_t i = 0; i < articles.size(); ++i) {
        db->AddArticle(articles[i]);
        db->SetRating(articles[i].link, static_cast<int>(i % 5) + 1);
    }
    AppCore core(regression_config(), std::move(db), std::make_unique<FetcherMock>());
    REQUIRE(core.IsRankerTrained()); // trained by the constructor, so the view scores

    core.ForceRetrain();
    for (int i = 0; i < 3000; ++i) {
        core.SetFilterIndex(AppCore::FilterView::All);
        core.SetFilterIndex(AppCore::FilterView::Recommended);
    }

    // The view still works after the retrain (the race itself is reported by
    // ThreadSanitizer, not by an assertion).
    REQUIRE(core.GetFilterView() == AppCore::FilterView::Recommended);
}

TEST_CASE("AppCore regression: a failed backfill does not mark the missed days as fetched",
          "[app][fetch][regression]") {
    // last_fetch_date used to be advanced before the fetch ran, so a network
    // failure on the first launch of the day lost every missed day for good.
    auto db = std::make_unique<DatabaseManager>(":memory:");
    db->SetMetadata("last_fetch_date", "2000-01-01");
    auto* db_raw = db.get();
    auto fetcher = std::make_unique<FetcherMock>();
    ALLOW_CALL(*fetcher, FetchSince(ANY(std::string)))
        .THROW(Arxiv::FetchError("arXiv API: HTTP 503"));

    std::unique_ptr<AppCore> core;
    REQUIRE_NOTHROW(
        core = std::make_unique<AppCore>(regression_config(), std::move(db), std::move(fetcher)));
    REQUIRE(db_raw->GetMetadata("last_fetch_date") == "2000-01-01");
}

TEST_CASE("AppCore regression: offline mode neither fetches nor moves the New Articles anchor",
          "[app][fetch][regression]") {
    // Headless --export-today/--export-yaml and --replay must not consume the
    // user's day-boundary crossing (or hit the network).
    auto db = std::make_unique<DatabaseManager>(":memory:");
    db->SetMetadata("last_fetch_date", "2000-01-01");
    db->SetMetadata("last_fetch_end_ts", "946771200");
    db->SetMetadata("new_articles_anchor_ts", "946684800");
    auto* db_raw = db.get();
    auto fetcher = std::make_unique<FetcherMock>();
    FORBID_CALL(*fetcher, Fetch());
    FORBID_CALL(*fetcher, FetchSince(ANY(std::string)));

    AppCore core(
        regression_config(), std::move(db), std::move(fetcher), AppCore::FetchMode::Offline);

    SECTION("last fetch date unchanged") {
        REQUIRE(db_raw->GetMetadata("last_fetch_date") == "2000-01-01");
    }
    SECTION("anchor unchanged") {
        REQUIRE(db_raw->GetMetadata("new_articles_anchor_ts") == "946684800");
    }
}

TEST_CASE("AppCore: re-linking an article does not duplicate it in the project's .bib",
          "[app][bibtex]") {
    auto article = make_article("2401.00050", "J. Doe", "hep-ph");
    auto db = std::make_unique<DatabaseManager>(":memory:");
    db->AddArticle(article);
    auto fetcher = std::make_unique<FetcherMock>();
    ALLOW_CALL(*fetcher, FetchBibTeX(ANY(std::string))).RETURN(std::string{});
    AppCore core(regression_config(), std::move(db), std::move(fetcher));
    core.AddProject("p");
    auto bib = std::filesystem::temp_directory_path() /
               ("arxiv_relink_" + std::to_string(::getpid()) + ".bib");
    REQUIRE(core.ExportProjectBibTeX("p", bib.string())); // empty; remembers the path

    core.LinkArticleToProject(article.link, "p");
    core.UnlinkArticleFromProject(article.link, "p");
    core.LinkArticleToProject(article.link, "p");

    std::ifstream in(bib);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t entries = 0;
    for (auto pos = content.find("@article{"); pos != std::string::npos;
         pos = content.find("@article{", pos + 1))
        ++entries;
    REQUIRE(entries == 1);
    std::filesystem::remove(bib);
}

TEST_CASE("AppCore: an export whose writes fail reports failure", "[app][export]") {
    // /dev/full opens fine and then fails every write (like a full disk);
    // the export used to report success regardless.
    if (!std::filesystem::is_character_file("/dev/full"))
        SKIP("/dev/full not available");
    auto article = make_article("2401.00060", "J. Doe", "hep-ph");
    auto core = make_memory_core({article});
    core->AddProject("p");
    core->LinkArticleToProject(article.link, "p");

    SECTION("Markdown") { REQUIRE_FALSE(core->ExportProjectMarkdown("p", "/dev/full")); }
    SECTION("JSON") { REQUIRE_FALSE(core->ExportProjectJSON("p", "/dev/full")); }
    SECTION("daily digest") { REQUIRE_FALSE(core->ExportDailyDigest("/dev/full")); }
}

TEST_CASE("AppCore: adding a tag keeps the current project view selected", "[app][project]") {
    // Tags sit before projects in the filter list, so a new tag shifted every
    // project index and the view silently switched to a different project.
    auto core = make_memory_core();
    core->AddProject("alpha");
    core->AddProject("beta");
    const auto& opts = core->GetFilterOptions();
    const auto beta = std::find(opts.begin(), opts.end(), "beta") - opts.begin();
    core->SetFilterIndex(static_cast<int>(beta));
    REQUIRE(core->GetProjectNameForFilter(core->GetFilterIndex()) == "beta");

    core->AddTag("to-read");

    REQUIRE(core->GetFilterView() == AppCore::FilterView::Project);
    REQUIRE(core->GetProjectNameForFilter(core->GetFilterIndex()) == "beta");
}

TEST_CASE("AppCore: removing the project being viewed refreshes the list", "[app][project]") {
    auto article = make_article("2401.00070", "J. Doe", "hep-ph");
    auto core = make_memory_core({article});
    core->AddProject("p");
    const auto& opts = core->GetFilterOptions();
    core->SetFilterIndex(static_cast<int>(std::find(opts.begin(), opts.end(), "p") - opts.begin()));
    REQUIRE(core->GetCurrentArticles().empty()); // nothing linked yet

    core->RemoveProject("p");

    REQUIRE(core->GetFilterView() == AppCore::FilterView::NewArticles);
    REQUIRE(core->GetCurrentArticles().size() == 1); // the New Articles list, not the stale one
}

TEST_CASE("AppCore: ImportProjectJSON accepts /pdf/ links", "[app][import][links]") {
    auto core = make_memory_core();
    auto path = write_temp_json(
        "pdf_links",
        R"({"name": "p", "articles": [{"link": "https://arxiv.org/pdf/2401.00009v3.pdf"}]})");

    REQUIRE(core->ImportProjectJSON(path));
    auto imported = core->GetArticlesForProject("p");
    REQUIRE(imported.size() == 1);
    REQUIRE(imported[0].link == "https://arxiv.org/abs/2401.00009");
    std::filesystem::remove(path);
}

TEST_CASE("AppCore: following an author by initial picks out that author", "[app][author]") {
    auto wei = make_article("2401.00080", "Wei Li, A. Other", "hep-ph");
    auto xiao = make_article("2401.00081", "Xiao Li, B. Other", "hep-ph");
    auto core = make_memory_core({wei, xiao});

    core->FollowAuthor("W. Li");
    auto followed = core->GetArticlesForFollowedAuthors();

    REQUIRE(followed.size() == 1);
    REQUIRE(followed[0].link == wei.link);
}

TEST_CASE("AppCore: following an author updates the Followed Authors view at once",
          "[app][author]") {
    auto article = make_article("2401.00090", "Wei Li, A. Other", "hep-ph");
    auto core = make_memory_core({article});
    core->SetFilterIndex(AppCore::FilterView::FollowedAuthors);
    REQUIRE(core->GetCurrentArticles().empty());

    core->FollowAuthor("W. Li");
    SECTION("follow") { REQUIRE(core->GetCurrentArticles().size() == 1); }
    SECTION("unfollow") {
        core->UnfollowAuthor("W. Li");
        REQUIRE(core->GetCurrentArticles().empty());
    }
}

TEST_CASE("Exports show an article's arXiv day in any time zone", "[app][export][regression]") {
    arxiv_tui::test::ScopedTimeZone tz("America/Los_Angeles");
    auto article = make_article("2607.00001", "J. Doe", "hep-ph");
    article.date = arxiv_tui::test::utc_time(2026, 7, 4, 4); // announced 4 July
    auto core = make_memory_core({article});
    core->AddProject("p");
    core->LinkArticleToProject(article.link, "p");
    auto path = std::filesystem::temp_directory_path() /
                ("arxiv_export_tz_" + std::to_string(::getpid()) + ".md");

    REQUIRE(core->ExportProjectMarkdown("p", path.string()));
    std::ifstream f(path);
    std::stringstream content;
    content << f.rdbuf();
    std::filesystem::remove(path);

    REQUIRE_THAT(content.str(), ContainsSubstring("2026-07-04"));
}

// ---------------------------------------------------------------------------
// Keyword boosts are the cold-start signal: before any star ratings they are
// all the ranker has, so the TUI's ranked views must use them.
// ---------------------------------------------------------------------------

namespace {
struct KeywordCore {
    Article match = make_article("2607.00010", "A. Author", "hep-ph");
    Article other = make_article("2607.00011", "B. Author", "hep-ph");
    std::unique_ptr<AppCore> core;

    KeywordCore() {
        match.title = "Sterile neutrino oscillations at short baselines";
        other.title = "Lattice study of glueballs";
        Config cfg = regression_config();
        cfg.set_keywords_file((std::filesystem::temp_directory_path() /
                               ("arxiv_kw_view_" + std::to_string(::getpid()) + ".txt"))
                                  .string());
        core = make_memory_core({other, match}, cfg);
        REQUIRE(core->SaveKeywords({"neutrino"}));
    }
};
} // namespace

TEST_CASE("Keyword boosts rank the TUI views before any ratings", "[app][keywords][regression]") {
    KeywordCore t;

    SECTION("Recommended shows the matching paper") {
        t.core->SetFilterIndex(AppCore::FilterView::Recommended);
        auto shown = t.core->GetCurrentArticles();
        REQUIRE(shown.size() == 1);
        REQUIRE(shown[0].link == t.match.link);
    }
    SECTION("New Articles puts the matching paper first") {
        t.core->SetFilterIndex(AppCore::FilterView::NewArticles);
        auto shown = t.core->GetCurrentArticles();
        REQUIRE(shown.size() == 2);
        REQUIRE(shown[0].link == t.match.link);
    }
    SECTION("the score column shows the keyword score") {
        REQUIRE(t.core->GetPredictedScore(t.match) > t.core->GetPredictedScore(t.other));
    }
}

// ---------------------------------------------------------------------------
// Nothing is written to the working directory: exports started from the TUI
// go to export_dir, and the ranker / fetcher have no relative defaults.
// ---------------------------------------------------------------------------

namespace {
// Runs the test body with the working directory set to a fresh temp dir.
struct InTempCwd {
    std::filesystem::path old = std::filesystem::current_path();
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("arxiv_cwd_" + std::to_string(::getpid()));
    InTempCwd() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        std::filesystem::current_path(dir);
    }
    ~InTempCwd() {
        std::filesystem::current_path(old);
        std::filesystem::remove_all(dir);
    }
    bool Empty() const { return std::filesystem::is_empty(dir); }
};
} // namespace

TEST_CASE("AppCore::ExportPath", "[app][export]") {
    auto dir =
        std::filesystem::temp_directory_path() / ("arxiv_export_dir_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    Config cfg = regression_config();
    cfg.set_export_dir(dir.string());
    auto core = make_memory_core({}, cfg);

    SECTION("puts the file in export_dir and creates it") {
        auto path = core->ExportPath("digest.md");
        REQUIRE(std::filesystem::path(path) == dir / "digest.md");
        REQUIRE(std::filesystem::is_directory(dir));
    }
    SECTION("a project name cannot leave export_dir") {
        auto path = std::filesystem::path(core->ExportPath("../Sub/Project.md"));
        REQUIRE(path.parent_path() == dir);
        REQUIRE(path.filename().string().find('/') == std::string::npos);
    }
    SECTION("falls back to download_dir when export_dir is not set") {
        Config plain = regression_config();
        plain.set_download_dir(dir.string());
        auto other = make_memory_core({}, plain);
        REQUIRE(std::filesystem::path(other->ExportPath("x.bib")).parent_path() == dir);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("Nothing is written to the working directory by default", "[app][regression]") {
    InTempCwd cwd;

    SECTION("a Fetcher without a download directory creates none") {
        Arxiv::Fetcher fetcher({"hep-ph"});
        REQUIRE(cwd.Empty());
    }
    SECTION("an AppCore without a ranker file saves no ranker") {
        Config cfg;
        cfg.set_topics({"hep-ph"});
        {
            AppCore core(cfg,
                         std::make_unique<Arxiv::DatabaseManager>(":memory:"),
                         std::make_unique<FetcherMock>(),
                         AppCore::FetchMode::Offline);
            core.ForceRetrain();
        } // joins the training thread
        REQUIRE(cwd.Empty());
    }
}

TEST_CASE("AppCore::CurrentProject / CurrentTag name the selected view", "[app][filter]") {
    auto article = make_article("2607.00020", "J. Doe", "hep-ph");
    auto core = make_memory_core({article});
    core->AddProject("Thesis");
    core->AddTag("flavour");
    core->LinkArticleToTag(article.link, "flavour");
    const auto& options = core->GetFilterOptions();
    auto index_of = [&](const std::string& label) {
        return static_cast<int>(
            std::distance(options.begin(), std::find(options.begin(), options.end(), label)));
    };

    SECTION("a fixed view has neither") {
        core->SetFilterIndex(AppCore::FilterView::All);
        REQUIRE(core->CurrentProject().empty());
        REQUIRE(core->CurrentTag().empty());
    }
    SECTION("a project view names the project") {
        core->SetFilterIndex(index_of("Thesis"));
        REQUIRE(core->GetFilterView() == AppCore::FilterView::Project);
        REQUIRE(core->CurrentProject() == "Thesis");
        REQUIRE(core->CurrentTag().empty());
    }
    SECTION("a tag view names the tag") {
        core->SetFilterIndex(index_of("#flavour"));
        REQUIRE(core->GetFilterView() == AppCore::FilterView::Tag);
        REQUIRE(core->CurrentTag() == "flavour");
        REQUIRE(core->CurrentProject().empty());
    }
}

TEST_CASE("A full retrain keeps the interest keywords", "[app][keywords][regression]") {
    // ForceRetrain swapped in a fresh Ranker, dropping the fitted keywords.
    KeywordCore t;
    REQUIRE(t.core->GetPredictedScore(t.match) > t.core->GetPredictedScore(t.other));

    t.core->ForceRetrain();
    for (int i = 0; i < 500 && t.core->IsTraining(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE_FALSE(t.core->IsTraining());

    REQUIRE(t.core->GetPredictedScore(t.match) > t.core->GetPredictedScore(t.other));
}

// ---------------------------------------------------------------------------
// Tagging from the TUI: ToggleTagOnTargets acts on the selection, or the
// focused article when nothing is selected.
// ---------------------------------------------------------------------------

namespace {
struct TagCore {
    Article a = make_article("2607.00030", "A. Author", "hep-ph");
    Article b = make_article("2607.00031", "B. Author", "hep-ph");
    std::unique_ptr<AppCore> core = make_memory_core({a, b});
    TagCore() { core->SetFilterIndex(AppCore::FilterView::All); }
    bool Has(const Article& art, const std::string& tag) const {
        auto tags = core->GetTagsForArticle(art.link);
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    }
    const Article& Focused() const {
        return core->GetCurrentArticles()[static_cast<size_t>(core->GetArticleIndex())];
    }
};
} // namespace

TEST_CASE("AppCore::ToggleTagOnTargets", "[app][tags]") {
    TagCore t;

    SECTION("tags the focused article") {
        t.core->ToggleTagOnTargets("flavour");
        REQUIRE(t.Has(t.Focused(), "flavour"));
    }
    SECTION("a new tag appears in the filter list at once") {
        t.core->ToggleTagOnTargets("flavour");
        const auto& options = t.core->GetFilterOptions();
        REQUIRE(std::find(options.begin(), options.end(), "#flavour") != options.end());
    }
    SECTION("toggling again removes it") {
        t.core->ToggleTagOnTargets("flavour");
        t.core->ToggleTagOnTargets("flavour");
        REQUIRE_FALSE(t.Has(t.Focused(), "flavour"));
    }
    SECTION("a selection where only some have the tag gets it on all") {
        t.core->LinkArticleToTag(t.a.link, "flavour");
        t.core->ToggleSelection(t.a.link);
        t.core->ToggleSelection(t.b.link);
        REQUIRE(t.core->TagCoverage("flavour") == AppCore::Coverage::Some);
        t.core->ToggleTagOnTargets("flavour");
        REQUIRE(t.Has(t.a, "flavour"));
        REQUIRE(t.Has(t.b, "flavour"));
        REQUIRE(t.core->TagCoverage("flavour") == AppCore::Coverage::All);
    }
    SECTION("untagging in the tag's own view takes the article off the list") {
        t.core->ToggleTagOnTargets("flavour");
        const auto& options = t.core->GetFilterOptions();
        t.core->SetFilterIndex(static_cast<int>(
            std::distance(options.begin(), std::find(options.begin(), options.end(), "#flavour"))));
        REQUIRE(t.core->GetCurrentArticles().size() == 1);
        t.core->ToggleTagOnTargets("flavour");
        REQUIRE(t.core->GetCurrentArticles().empty());
    }
}

TEST_CASE("AppCore::LinkArticleToTag lists a new tag in the filters", "[app][tags][regression]") {
    // Linking created the tag in the database but the filter list was only
    // rebuilt on the next start.
    TagCore t;
    t.core->LinkArticleToTag(t.a.link, "lattice");
    const auto& options = t.core->GetFilterOptions();
    REQUIRE(std::find(options.begin(), options.end(), "#lattice") != options.end());
}

TEST_CASE("AppCore: adding a selection to a project updates the project's .bib",
          "[app][bibtex][regression]") {
    // Single assignment appended to the exported .bib; bulk assignment did not.
    auto a = make_article("2401.00051", "J. Doe", "hep-ph");
    auto b = make_article("2401.00052", "R. Roe", "hep-ph");
    auto db = std::make_unique<DatabaseManager>(":memory:");
    db->AddArticle(a);
    db->AddArticle(b);
    auto fetcher = std::make_unique<FetcherMock>();
    ALLOW_CALL(*fetcher, FetchBibTeX(ANY(std::string))).RETURN(std::string{});
    AppCore core(regression_config(), std::move(db), std::move(fetcher));
    core.AddProject("p");
    auto bib = std::filesystem::temp_directory_path() /
               ("arxiv_bulk_bib_" + std::to_string(::getpid()) + ".bib");
    REQUIRE(core.ExportProjectBibTeX("p", bib.string()));

    core.ToggleSelection(a.link);
    core.ToggleSelection(b.link);
    core.AddSelectedToProject("p");

    std::ifstream in(bib);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::filesystem::remove(bib);
    REQUIRE_THAT(content, ContainsSubstring(a.id()));
    REQUIRE_THAT(content, ContainsSubstring(b.id()));
}

TEST_CASE("Project JSON export writes invalid UTF-8 from a feed instead of throwing",
          "[export][regression]") {
    // pugixml passes invalid bytes through and the database keeps them;
    // json::dump then threw and took the TUI down.
    auto dir =
        std::filesystem::temp_directory_path() / ("utf8_export_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    auto paper = arxiv_tui::test::fixtures::sample_articles[0];
    paper.title = "Broken \xC3 title";
    const auto path = (dir / "p.json").string();

    bool ok = false;
    REQUIRE_NOTHROW(ok = Arxiv::Export::ProjectJSON(
                        path, "P", {paper}, [](const std::string&) { return std::string(); }));
    REQUIRE(ok);
    std::filesystem::remove_all(dir);
}

namespace {
std::unique_ptr<Arxiv::AppCore> core_with_papers(int n) {
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    auto base = arxiv_tui::test::fixtures::sample_articles[0];
    for (int i = 0; i < n; ++i) {
        auto a = base;
        a.link = "https://arxiv.org/abs/2607." + std::to_string(20000 + i);
        a.title = "Paper " + std::to_string(i);
        a.date = base.date - std::chrono::hours(i); // a fixed order
        db->AddArticle(a);
    }
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    auto core = std::make_unique<Arxiv::AppCore>(
        cfg, std::move(db), std::make_unique<FetcherMock>(), Arxiv::AppCore::FetchMode::Offline);
    core->SetFilterIndex(Arxiv::AppCore::FilterView::All);
    return core;
}
} // namespace

TEST_CASE("Reloading a view keeps the cursor on the same paper", "[appcore][regression]") {
    // FetchArticles reset the cursor to the top: every auto-refresh,
    // retrain, tag change, delete or undo sent the user back to row 0.
    auto core = core_with_papers(4);
    core->SetArticleIndex(2);
    const auto focused = core->GetCurrentArticles()[2].link;

    SECTION("same view reloaded") {
        core->AddTag("x"); // reloads the view
        REQUIRE(
            core->GetCurrentArticles()[static_cast<std::size_t>(core->GetArticleIndex())].link ==
            focused);
    }
    SECTION("focused paper deleted: the cursor stays on that row") {
        core->DeleteCurrentOrSelected();
        REQUIRE(core->GetArticleIndex() == 2);
    }
    SECTION("last paper deleted: the cursor moves up one") {
        core->SetArticleIndex(3);
        core->DeleteCurrentOrSelected();
        REQUIRE(core->GetArticleIndex() == 2);
    }
    SECTION("another view starts at the top") {
        core->SetFilterIndex(Arxiv::AppCore::FilterView::Unread);
        REQUIRE(core->GetArticleIndex() == 0);
    }
    SECTION("a new search starts at the top") {
        core->SetFilterIndex(Arxiv::AppCore::FilterView::Search);
        core->SetSearchQuery("Paper", true, false, false);
        core->SetArticleIndex(2);
        core->SetSearchQuery("Paper", true, false, false);
        REQUIRE(core->GetArticleIndex() == 0);
    }
}

TEST_CASE("Obsidian notes keep feed text inside its frontmatter values",
          "[export][security][regression]") {
    // The category went into the frontmatter unquoted: newlines in it added
    // keys or closed the frontmatter early. Control bytes broke the YAML.
    auto vault =
        std::filesystem::temp_directory_path() / ("vault_yaml_" + std::to_string(::getpid()));
    std::filesystem::remove_all(vault);
    auto paper = arxiv_tui::test::fixtures::sample_articles[0];
    paper.category = "hep-ph\nevil: injected\n---\n# Owned";
    paper.title = "Title \x1b[2J with escape";
    FetcherMock fetcher;

    Arxiv::Export::ObsidianNotes(fetcher, {paper}, vault.string(), false, {});

    std::filesystem::path note;
    for (const auto& e : std::filesystem::recursive_directory_iterator(vault))
        if (e.path().filename() == paper.file_stem() + ".md")
            note = e.path();
    std::ifstream in(note);
    std::string line, front;
    std::getline(in, line); // opening ---
    while (std::getline(in, line) && line != "---")
        front += line + "\n";
    const YAML::Node yaml = YAML::Load(front);

    SECTION("no injected key") { REQUIRE_FALSE(yaml["evil"]); }
    SECTION("the category stays one value") {
        REQUIRE(yaml["category"].as<std::string>().find("evil") != std::string::npos);
    }
    SECTION("no control characters") { REQUIRE(front.find('\x1b') == std::string::npos); }
    std::filesystem::remove_all(vault);
}

TEST_CASE("Only arXiv links are handed to the browser", "[appcore][security][regression]") {
    // Rows stored before links were validated (or only normalised by a
    // migration) could hold file://, javascript: or "-..." values, which
    // went to xdg-open as they were.
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    auto good = arxiv_tui::test::fixtures::sample_articles[0];
    auto bad = good;
    bad.link = "file:///etc/passwd";
    db->AddArticle(good);
    db->AddArticle(bad);
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    Arxiv::AppCore core(
        cfg, std::move(db), std::make_unique<FetcherMock>(), Arxiv::AppCore::FetchMode::Offline);
    core.SetFilterIndex(Arxiv::AppCore::FilterView::All);
    core.ToggleSelection(good.link);
    core.ToggleSelection(bad.link);

    REQUIRE(core.GetLinksToOpen() == std::vector<std::string>{good.link});
}

TEST_CASE("Search covers every field asked for", "[appcore][search][regression]") {
    // SetSearchQuery kept only the first field set: title+authors+abstract
    // (what the GUI asks for) searched titles only.
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    for (const auto& a : arxiv_tui::test::fixtures::sample_articles)
        db->AddArticle(a);
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    Arxiv::AppCore core(
        cfg, std::move(db), std::make_unique<FetcherMock>(), Arxiv::AppCore::FetchMode::Offline);
    core.SetFilterIndex(Arxiv::AppCore::FilterView::Search);

    core.SetSearchQuery("purposes", true, true, true); // only in an abstract

    REQUIRE(core.GetCurrentArticles().size() == 1);
}

TEST_CASE("Search falls back to fuzzy matching", "[appcore][search]") {
    // The docs promised near-miss results; FuzzySearchArticles was never used.
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    for (const auto& a : arxiv_tui::test::fixtures::sample_articles)
        db->AddArticle(a);
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    Arxiv::AppCore core(
        cfg, std::move(db), std::make_unique<FetcherMock>(), Arxiv::AppCore::FetchMode::Offline);
    core.SetFilterIndex(Arxiv::AppCore::FilterView::Search);

    core.SetSearchQuery("Artcle", true, false, false); // typo of "Article"

    REQUIRE_FALSE(core.GetCurrentArticles().empty());
}

TEST_CASE("Papers a cron --fetch stored stay new until the user opens the TUI",
          "[appcore][new_articles][regression]") {
    // A headless --fetch crossing a day moved the New Articles anchor to the
    // previous fetch's end, as an interactive session does. With a daily
    // cron, day 2's run moved it past what day 1's run had stored, so those
    // papers never showed as new.
    const auto dir =
        std::filesystem::temp_directory_path() / ("cron_anchor_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string db_file = (dir / "articles.db").string();
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    const auto paper = arxiv_tui::test::fixtures::sample_articles[0];
    {
        // The user's last session, long ago.
        Arxiv::DatabaseManager db(db_file);
        db.SetMetadata("last_fetch_date", "2000-01-01");
        db.SetMetadata("last_fetch_end_ts", "100");
        db.SetMetadata("new_articles_anchor_ts", "50");
    }
    auto cron_day = [&](std::vector<Arxiv::Article> fetched) {
        {
            // Each run is a new day.
            Arxiv::DatabaseManager db(db_file);
            db.SetMetadata("last_fetch_date", "2000-01-02");
        }
        auto fetcher = std::make_unique<FetcherMock>();
        ALLOW_CALL(*fetcher, FetchSince(ANY(std::string))).RETURN(fetched);
        Arxiv::AppCore cron(cfg,
                            std::make_unique<Arxiv::DatabaseManager>(db_file),
                            std::move(fetcher),
                            Arxiv::AppCore::FetchMode::Headless);
    };
    // Day 2's paper keeps the view from falling back to its date window
    // (used only when the anchored window is empty).
    const auto day2_paper = arxiv_tui::test::fixtures::sample_articles[1];
    cron_day({paper});                                            // day 1
    std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // next second
    cron_day({day2_paper});                                       // day 2

    Arxiv::AppCore tui(cfg,
                       std::make_unique<Arxiv::DatabaseManager>(db_file),
                       std::make_unique<FetcherMock>(),
                       Arxiv::AppCore::FetchMode::Sync);
    tui.SetFilterIndex(Arxiv::AppCore::FilterView::NewArticles);

    const auto& shown = tui.GetCurrentArticles();
    REQUIRE(std::any_of(
        shown.begin(), shown.end(), [&](const Arxiv::Article& a) { return a.link == paper.link; }));
    std::filesystem::remove_all(dir);
}

TEST_CASE("Undo brings back a deleted paper's tags in the filter pane",
          "[appcore][undo][regression]") {
    auto core = core_with_papers(2);
    const auto link = core->GetCurrentArticles()[0].link;
    core->LinkArticleToTag(link, "keep");
    core->DeleteCurrentOrSelected();
    core->RemoveTag("keep");

    core->UndoLastDelete();

    const auto& options = core->GetFilterOptions();
    REQUIRE(std::find(options.begin(), options.end(), "#keep") != options.end());
}

TEST_CASE("Un-bookmarking in the Bookmarks view removes the paper from it",
          "[appcore][bookmarks][regression]") {
    auto core = core_with_papers(3);
    for (const auto& a : std::vector<Arxiv::Article>(core->GetCurrentArticles()))
        core->ToggleBookmark(a.link);
    core->SetFilterIndex(Arxiv::AppCore::FilterView::Bookmarks);
    REQUIRE(core->GetCurrentArticles().size() == 3);

    core->ToggleBookmark(core->GetCurrentArticles()[1].link);

    REQUIRE(core->GetCurrentArticles().size() == 2);
}

namespace {
std::vector<std::string> sorted(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    return v;
}
} // namespace

TEST_CASE("Renaming and moving projects and tags", "[appcore][rename]") {
    auto core = core_with_papers(2);
    core->AddProject("thesis");
    core->AddProject("chapter");
    core->SetProjectParent("chapter", "thesis");
    core->AddProject("section");
    core->SetProjectParent("section", "chapter");
    core->AddTag("qcd");

    auto select = [&](const std::string& entry) {
        const auto& options = core->GetFilterOptions();
        const auto it = std::find_if(options.begin(), options.end(), [&](const std::string& o) {
            return o.find(entry) != std::string::npos;
        });
        core->SetFilterIndex(static_cast<int>(it - options.begin()));
    };

    SECTION("a renamed project stays selected") {
        select("thesis");
        REQUIRE(core->RenameProject("thesis", "  dissertation "));
        REQUIRE(core->CurrentProject() == "dissertation");
    }
    SECTION("a taken name is refused") { REQUIRE_FALSE(core->RenameProject("thesis", "chapter")); }
    SECTION("a renamed tag stays selected") {
        select("#qcd");
        REQUIRE(core->RenameTag("qcd", "lattice"));
        REQUIRE(core->CurrentTag() == "lattice");
    }
    SECTION("a project cannot move under itself or its sub-projects") {
        REQUIRE(sorted(core->ParentCandidates("thesis")) == std::vector<std::string>{""});
        REQUIRE(sorted(core->ParentCandidates("section")) ==
                std::vector<std::string>{"", "chapter", "thesis"});
    }
}

TEST_CASE("Project JSON export and import round-trip", "[appcore][export][import]") {
    // The export dropped the parent, categories, tags and ratings, and an
    // entry without a date was imported as 1 January 1970.
    const auto dir =
        std::filesystem::temp_directory_path() / ("json_roundtrip_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const auto file = (dir / "p.json").string();
    auto paper = arxiv_tui::test::fixtures::sample_articles[0];
    paper.category = "hep-ph";
    {
        auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
        db->AddArticle(paper);
        db->SetRating(paper.link, 4);
        Arxiv::Config cfg;
        cfg.set_topics({"hep-ph"});
        Arxiv::AppCore core(cfg,
                            std::move(db),
                            std::make_unique<FetcherMock>(),
                            Arxiv::AppCore::FetchMode::Offline);
        core.AddProject("Parent");
        core.AddProject("P");
        core.SetProjectParent("P", "Parent");
        core.LinkArticleToProject(paper.link, "P");
        core.LinkArticleToTag(paper.link, "qcd");
        REQUIRE(core.ExportProjectJSON("P", file));
    }
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    auto* raw = db.get();
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    Arxiv::AppCore core(
        cfg, std::move(db), std::make_unique<FetcherMock>(), Arxiv::AppCore::FetchMode::Offline);

    REQUIRE(core.ImportProjectJSON(file));

    SECTION("parent") { REQUIRE(raw->GetProjectParent("P") == "Parent"); }
    SECTION("category") { REQUIRE(raw->GetArticle(paper.link)->category == "hep-ph"); }
    SECTION("tags") {
        REQUIRE(raw->GetTagsForArticle(paper.link) == std::vector<std::string>{"qcd"});
    }
    SECTION("rating") { REQUIRE(raw->GetRating(paper.link) == 4); }
    std::filesystem::remove_all(dir);
}

TEST_CASE("Project JSON import: missing dates and blank names", "[appcore][import][regression]") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("json_import_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    auto core = core_with_papers(0);

    SECTION("an entry without a date is dated now, not 1970") {
        auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
        auto* raw = db.get();
        Arxiv::Config cfg;
        cfg.set_topics({"hep-ph"});
        Arxiv::AppCore fresh(cfg,
                             std::move(db),
                             std::make_unique<FetcherMock>(),
                             Arxiv::AppCore::FetchMode::Offline);
        const auto file = (dir / "nodate.json").string();
        std::ofstream(file)
            << R"({"name":"P","articles":[{"link":"https://arxiv.org/abs/2501.00001","title":"T"}]})";
        REQUIRE(fresh.ImportProjectJSON(file));
        const auto stored = raw->GetArticle("https://arxiv.org/abs/2501.00001");
        REQUIRE(stored.has_value());
        REQUIRE(stored->date > std::chrono::system_clock::now() - std::chrono::hours(24));
    }
    SECTION("a blank project name is refused") {
        const auto file = (dir / "blank.json").string();
        std::ofstream(file) << R"({"name":"   ","articles":[]})";
        REQUIRE_FALSE(core->ImportProjectJSON(file));
        REQUIRE(core->GetProjects().empty());
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("StartRefresh fetches in the background", "[appcore][fetch]") {
    // The GUI's Refresh only re-read the database; a network refresh existed
    // only as the timed auto-refresh.
    auto fetcher = std::make_unique<FetcherMock>();
    const auto paper = arxiv_tui::test::fixtures::sample_articles[0];
    ALLOW_CALL(*fetcher, Fetch()).RETURN(std::vector<Arxiv::Article>{paper});
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    Arxiv::AppCore core(cfg,
                        std::make_unique<Arxiv::DatabaseManager>(":memory:"),
                        std::move(fetcher),
                        Arxiv::AppCore::FetchMode::Offline);
    core.SetFilterIndex(Arxiv::AppCore::FilterView::All);
    REQUIRE(core.GetCurrentArticles().empty());

    REQUIRE(core.StartRefresh());
    core.WaitForRefresh();
    core.TryRefetchIfNeeded();

    REQUIRE(core.GetCurrentArticles().size() == 1);
}
