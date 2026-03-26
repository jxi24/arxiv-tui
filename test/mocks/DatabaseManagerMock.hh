#pragma once

#include <Arxiv/DatabaseManager.hh>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <unordered_set>

namespace arxiv_tui {
namespace test {

class DatabaseManagerMock : public Arxiv::DatabaseManager {
public:
    // Constructor
    DatabaseManagerMock() : Arxiv::DatabaseManager(":memory:") {
        // Default: rated articles list is empty unless overridden
        ALLOW_CALL(*this, GetRatedArticles())
            .RETURN(std::vector<std::pair<Arxiv::Article, int>>{});
        ALLOW_CALL(*this, GetRating(ANY(std::string)))
            .RETURN(0);
    }

    // Mock methods using trompeloeil
    MAKE_MOCK1(AddArticle, void(const Arxiv::Article&), override);
    MAKE_MOCK1(GetRecent, std::vector<Arxiv::Article>(int), override);
    MAKE_MOCK0(ListBookmarked, std::vector<Arxiv::Article>(), override);
    MAKE_MOCK1(GetArticlesForProject, std::vector<Arxiv::Article>(const std::string&), override);
    MAKE_MOCK2(ToggleBookmark, void(const std::string&, bool), override);
    MAKE_MOCK1(AddProject, void(const std::string&), override);
    MAKE_MOCK1(RemoveProject, void(const std::string&), override);
    MAKE_MOCK0(GetProjects, std::vector<std::string>(), override);
    MAKE_MOCK2(LinkArticleToProject, void(const std::string&, const std::string&), override);
    MAKE_MOCK2(UnlinkArticleFromProject, void(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetProjectsForArticle, std::vector<std::string>(const std::string&), override);
    MAKE_MOCK2(GetArticlesForDateRange, std::vector<Arxiv::Article>(const std::string&, const std::string&), override);
    MAKE_MOCK4(SearchArticles, std::vector<Arxiv::Article>(const std::string&, bool, bool, bool), override);

    // Rating mocks
    MAKE_MOCK2(SetRating, void(const std::string&, int), override);
    MAKE_MOCK1(GetRating, int(const std::string&), override);
    MAKE_MOCK0(GetRatedArticles, std::vector<std::pair<Arxiv::Article, int>>(), override);

    // Helper methods to set up mock responses
    void setArticles(const std::vector<Arxiv::Article>& articles) {
        m_articles = articles;
        ALLOW_CALL(*this, GetRecent(ANY(int)))
            .RETURN(articles);
    }

    void setBookmarkedArticles(const std::vector<Arxiv::Article>& articles) {
        m_bookmarked_articles = articles;
        ALLOW_CALL(*this, ListBookmarked())
            .RETURN(articles);
    }

    void setProjectArticles(const std::string& project, const std::vector<Arxiv::Article>& articles) {
        m_project_articles[project] = articles;
        ALLOW_CALL(*this, GetArticlesForProject(project))
            .RETURN(articles);
    }

    void setProjects(const std::vector<std::string>& projects) {
        m_projects = projects;
        ALLOW_CALL(*this, GetProjects())
            .RETURN(projects);
    }

    void setRatedArticles(const std::vector<std::pair<Arxiv::Article, int>>& rated) {
        m_rated_articles = rated;
        ALLOW_CALL(*this, GetRatedArticles())
            .RETURN(rated);
        for (const auto &[article, rating] : rated) {
            ALLOW_CALL(*this, GetRating(article.link))
                .RETURN(rating);
        }
    }

    void setBookmarkState(const std::string& article_link, bool is_bookmarked) {
        if (is_bookmarked) {
            auto it = std::find_if(m_articles.begin(), m_articles.end(),
                [&](const Arxiv::Article& a) { return a.link == article_link; });
            if (it != m_articles.end()) {
                m_bookmarked_articles.push_back(*it);
            }
        } else {
            m_bookmarked_articles.erase(
                std::remove_if(m_bookmarked_articles.begin(), m_bookmarked_articles.end(),
                    [&](const Arxiv::Article& a) { return a.link == article_link; }),
                m_bookmarked_articles.end());
        }
        ALLOW_CALL(*this, ToggleBookmark(article_link, is_bookmarked));
        ALLOW_CALL(*this, ListBookmarked())
            .RETURN(m_bookmarked_articles);
    }

private:
    std::vector<Arxiv::Article> m_articles;
    std::vector<Arxiv::Article> m_bookmarked_articles;
    std::vector<std::string> m_projects;
    std::unordered_map<std::string, std::vector<Arxiv::Article>> m_project_articles;
    std::vector<std::pair<Arxiv::Article, int>> m_rated_articles;
};

} // namespace test
} // namespace arxiv_tui 
