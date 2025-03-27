#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <Arxiv/DatabaseManager.hh>
#include <fixtures/test_data.hh>
#include <mocks/DatabaseManagerMock.hh>

using namespace arxiv_tui;
using namespace arxiv_tui::test;
using namespace Catch::Matchers;

TEST_CASE("Article management", "[database]") {
    DatabaseManagerMock db;
    
    SECTION("Should add and retrieve articles") {
        Arxiv::Article article = fixtures::sample_articles[0];
        
        // Set up mock expectations
        REQUIRE_CALL(db, AddArticle(article));
        REQUIRE_CALL(db, GetRecent(-1))
            .RETURN(std::vector<Arxiv::Article>{article});
        
        db.AddArticle(article);
        auto articles = db.GetRecent(-1); // Get all articles
        REQUIRE(articles.size() == 1);
        REQUIRE(articles[0].title == article.title);
    }

    SECTION("Should handle bookmarks") {
        Arxiv::Article article = fixtures::sample_articles[0];
        
        // Set up mock expectations for bookmarking
        REQUIRE_CALL(db, AddArticle(article));
        REQUIRE_CALL(db, ToggleBookmark(article.link, true));
        REQUIRE_CALL(db, ListBookmarked())
            .RETURN(std::vector<Arxiv::Article>{article});
        
        // Test bookmarking
        db.AddArticle(article);
        db.ToggleBookmark(article.link, true);
        auto bookmarked = db.ListBookmarked();
        REQUIRE(bookmarked.size() == 1);
        REQUIRE(bookmarked[0].link == article.link);
        
        // Set up mock expectations for unbookmarking
        REQUIRE_CALL(db, ToggleBookmark(article.link, false));
        REQUIRE_CALL(db, ListBookmarked())
            .RETURN(std::vector<Arxiv::Article>{});
        
        // Test unbookmarking
        db.ToggleBookmark(article.link, false);
        bookmarked = db.ListBookmarked();
        REQUIRE(bookmarked.empty());
    }
}

TEST_CASE("Project management", "[database]") {
    DatabaseManagerMock db;
    
    SECTION("Should manage projects") {
        std::string project_name = "Test Project";
        
        // Set up mock expectations for adding project
        REQUIRE_CALL(db, AddProject(project_name));
        REQUIRE_CALL(db, GetProjects())
            .RETURN(std::vector<std::string>{project_name});
        
        // Test adding project
        db.AddProject(project_name);
        auto projects = db.GetProjects();
        REQUIRE(projects.size() == 1);
        REQUIRE(projects[0] == project_name);
        
        // Set up mock expectations for removing project
        REQUIRE_CALL(db, RemoveProject(project_name));
        REQUIRE_CALL(db, GetProjects())
            .RETURN(std::vector<std::string>{});
        
        // Test removing project
        db.RemoveProject(project_name);
        projects = db.GetProjects();
        REQUIRE(projects.empty());
    }
    
    SECTION("Should link articles to projects") {
        Arxiv::Article article = fixtures::sample_articles[0];
        std::string project_name = "Test Project";
        
        // Set up mock expectations for linking
        REQUIRE_CALL(db, AddArticle(article));
        REQUIRE_CALL(db, AddProject(project_name));
        REQUIRE_CALL(db, LinkArticleToProject(article.link, project_name));
        REQUIRE_CALL(db, GetArticlesForProject(project_name))
            .RETURN(std::vector<Arxiv::Article>{article});
        
        // Test linking article to project
        db.AddArticle(article);
        db.AddProject(project_name);
        db.LinkArticleToProject(article.link, project_name);
        
        auto project_articles = db.GetArticlesForProject(project_name);
        REQUIRE(project_articles.size() == 1);
        REQUIRE(project_articles[0].link == article.link);
        
        // Set up mock expectations for unlinking
        REQUIRE_CALL(db, UnlinkArticleFromProject(article.link, project_name));
        REQUIRE_CALL(db, GetArticlesForProject(project_name))
            .RETURN(std::vector<Arxiv::Article>{});
        
        // Test unlinking article from project
        db.UnlinkArticleFromProject(article.link, project_name);
        project_articles = db.GetArticlesForProject(project_name);
        REQUIRE(project_articles.empty());
    }
} 
