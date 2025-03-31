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
        
        // Set up mock responses
        REQUIRE_CALL(*db_ptr, GetRecent(-1))
            .RETURN(sample_articles);
        REQUIRE_CALL(*db_ptr, ListBookmarked())
            .RETURN(std::vector<Arxiv::Article>{});
        REQUIRE_CALL(*db_ptr, GetProjects())
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
        
        core.SetFilterIndex(0); // All Articles
        auto all_articles = core.GetCurrentArticles();
        REQUIRE(all_articles.size() == sample_articles.size());
        
        core.SetFilterIndex(1); // Bookmarks
        auto filtered_bookmarked = core.GetCurrentArticles();
        REQUIRE(filtered_bookmarked.size() == bookmarked_articles.size());
        
        core.SetFilterIndex(2); // Today
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
        int initial_index = core.GetFilterIndex();
        core.SetFilterIndex(initial_index + 1);
        REQUIRE(core.GetFilterIndex() == initial_index + 1);
    }
}

// Note: Most of the ArxivApp's functionality is UI-based and involves
// private components that can't be directly tested in unit tests.
// The actual functionality should be tested through integration tests
// that simulate user interactions with the UI. 
