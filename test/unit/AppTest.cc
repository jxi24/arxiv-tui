#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <Arxiv/AppCore.hh>
#include <Arxiv/Config.hh>
#include <fixtures/test_data.hh>
#include <mocks/DatabaseManagerMock.hh>
#include <mocks/FetcherMock.hh>

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
        auto* fetcher_ptr = fetcher.get();

        // Expectations must live in the test scope (not inside helper functions)
        ALLOW_CALL(*fetcher_ptr, Fetch())
            .RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, AddArticle(trompeloeil::_));
        ALLOW_CALL(*db_ptr, GetRecent(trompeloeil::_))
            .RETURN(sample_articles);
        ALLOW_CALL(*db_ptr, ListBookmarked())
            .RETURN(std::vector<Arxiv::Article>{});
        ALLOW_CALL(*db_ptr, GetProjects())
            .RETURN(std::vector<std::string>{});

        Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

        auto articles = core.GetCurrentArticles();
        auto titles = core.GetCurrentTitles();
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
    ALLOW_CALL(*fetcher_ptr, Fetch())
        .RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, AddArticle(trompeloeil::_));
    ALLOW_CALL(*db_ptr, GetRecent(trompeloeil::_))
        .RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, ListBookmarked())
        .RETURN(std::vector<Arxiv::Article>{});
    ALLOW_CALL(*db_ptr, GetProjects())
        .RETURN(std::vector<std::string>{});
    ALLOW_CALL(*db_ptr, ToggleBookmark(trompeloeil::_, trompeloeil::_));

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Should handle article filtering") {
        core.SetFilterIndex(0); // All Articles
        auto all_articles = core.GetCurrentArticles();
        REQUIRE(all_articles.size() == sample_articles.size());

        core.SetFilterIndex(1); // Bookmarks
        auto filtered_bookmarked = core.GetCurrentArticles();
        // ListBookmarked returns empty
        REQUIRE(filtered_bookmarked.empty());

        core.SetFilterIndex(2); // Today
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

    ALLOW_CALL(*fetcher_ptr, Fetch())
        .RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, AddArticle(trompeloeil::_));
    ALLOW_CALL(*db_ptr, GetRecent(trompeloeil::_))
        .RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, ListBookmarked())
        .RETURN(std::vector<Arxiv::Article>{});
    ALLOW_CALL(*db_ptr, GetProjects())
        .LR_RETURN(mock_projects);
    ALLOW_CALL(*db_ptr, AddProject(trompeloeil::_));
    ALLOW_CALL(*db_ptr, RemoveProject(trompeloeil::_));
    ALLOW_CALL(*db_ptr, LinkArticleToProject(trompeloeil::_, trompeloeil::_));
    ALLOW_CALL(*db_ptr, UnlinkArticleFromProject(trompeloeil::_, trompeloeil::_));
    ALLOW_CALL(*db_ptr, GetArticlesForProject(trompeloeil::_))
        .LR_RETURN(mock_project_articles);

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
        REQUIRE(std::find_if(project_articles.begin(), project_articles.end(),
            [&](const Arxiv::Article& a) { return a.link == article_link; }) != project_articles.end());

        mock_project_articles = {};
        core.UnlinkArticleFromProject(article_link, project_name);
        project_articles = core.GetArticlesForProject(project_name);
        REQUIRE(std::find_if(project_articles.begin(), project_articles.end(),
            [&](const Arxiv::Article& a) { return a.link == article_link; }) == project_articles.end());
    }
}

TEST_CASE("AppCore state management", "[app]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();
    auto* fetcher_ptr = fetcher.get();

    ALLOW_CALL(*fetcher_ptr, Fetch())
        .RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, AddArticle(trompeloeil::_));
    ALLOW_CALL(*db_ptr, GetRecent(trompeloeil::_))
        .RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, ListBookmarked())
        .RETURN(std::vector<Arxiv::Article>{});
    ALLOW_CALL(*db_ptr, GetProjects())
        .RETURN(std::vector<std::string>{});

    Arxiv::AppCore core(config, std::move(db), std::move(fetcher));

    SECTION("Should handle article index changes") {
        auto articles = core.GetCurrentArticles();
        REQUIRE(!articles.empty());
        int initial_index = core.GetArticleIndex();
        core.SetArticleIndex(initial_index + 1);
        REQUIRE(core.GetArticleIndex() == initial_index + 1);
    }

    SECTION("Should handle filter index changes") {
        int initial_index = core.GetFilterIndex();
        core.SetFilterIndex(initial_index + 1);
        REQUIRE(core.GetFilterIndex() == initial_index + 1);
    }
}

TEST_CASE("AppCore BibTeX generation", "[app][bibtex]") {
    Config config("test/fixtures/test_config.yml");
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();
    auto* fetcher_ptr = fetcher.get();

    ALLOW_CALL(*fetcher_ptr, Fetch())
        .RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, AddArticle(trompeloeil::_));
    ALLOW_CALL(*db_ptr, GetRecent(trompeloeil::_))
        .RETURN(sample_articles);
    ALLOW_CALL(*db_ptr, ListBookmarked())
        .RETURN(std::vector<Arxiv::Article>{});
    ALLOW_CALL(*db_ptr, GetProjects())
        .RETURN(std::vector<std::string>{});

    SECTION("Should use INSPIRE BibTeX when available") {
        std::string inspire_bibtex = "@article{Doe:2403.12345,\n    author = \"Doe, John\",\n    title = \"{Sample}\"\n}";
        ALLOW_CALL(*fetcher_ptr, FetchBibtex(trompeloeil::_))
            .RETURN(inspire_bibtex);

        Arxiv::AppCore core(config, std::move(db), std::move(fetcher));
        auto articles = core.GetCurrentArticles();
        REQUIRE(!articles.empty());

        auto result = core.GetBibtex(articles[0]);
        REQUIRE(result == inspire_bibtex);
    }

    SECTION("Should fall back to constructed BibTeX when INSPIRE fails") {
        ALLOW_CALL(*fetcher_ptr, FetchBibtex(trompeloeil::_))
            .RETURN(std::string(""));

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

// Note: Most of the ArxivApp's functionality is UI-based and involves
// private components that can't be directly tested in unit tests.
// The actual functionality should be tested through integration tests
// that simulate user interactions with the UI.
