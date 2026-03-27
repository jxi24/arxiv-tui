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
        // Default article/project responses
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetRecent(ANY(int)))
                .RETURN(std::vector<Arxiv::Article>{}));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, ListBookmarked())
                .RETURN(std::vector<Arxiv::Article>{}));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetProjects())
                .RETURN(std::vector<std::string>{}));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, AddArticle(ANY(Arxiv::Article))));
        // Default: rated articles list is empty unless overridden
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetRatedArticles())
                .RETURN(Arxiv::DatabaseManager::RatedArticleList{}));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetRating(ANY(std::string)))
                .RETURN(0));
        // Default: all projects are top-level (no parent)
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetProjectParent(ANY(std::string)))
                .RETURN(std::string{}));
        // Default: notes return empty
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetProjectNote(ANY(std::string), ANY(std::string)))
                .RETURN(std::string{}));
        // Default: mutation methods are allowed (no-ops)
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, ToggleBookmark(ANY(std::string), ANY(bool))));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, AddProject(ANY(std::string))));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, RemoveProject(ANY(std::string))));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, LinkArticleToProject(ANY(std::string), ANY(std::string))));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, UnlinkArticleFromProject(ANY(std::string), ANY(std::string))));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, SetProjectParent(ANY(std::string), ANY(std::string))));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, SetProjectNote(ANY(std::string), ANY(std::string), ANY(std::string))));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, SetRating(ANY(std::string), ANY(int))));
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

    MAKE_MOCK1(GetProjectParent, std::string(const std::string&), override);
    MAKE_MOCK2(SetProjectParent, void(const std::string&, const std::string&), override);
    MAKE_MOCK3(SetProjectNote, void(const std::string&, const std::string&, const std::string&), override);
    MAKE_MOCK2(GetProjectNote, std::string(const std::string&, const std::string&), override);

    // Rating mocks
    MAKE_MOCK2(SetRating, void(const std::string&, int), override);
    MAKE_MOCK1(GetRating, int(const std::string&), override);
    MAKE_MOCK0(GetRatedArticles, Arxiv::DatabaseManager::RatedArticleList(), override);

    // Helper methods to set up mock responses
    void setArticles(const std::vector<Arxiv::Article>& articles) {
        m_articles = articles;
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetRecent(ANY(int)))
                .RETURN(articles));
    }

    void setBookmarkedArticles(const std::vector<Arxiv::Article>& articles) {
        m_bookmarked_articles = articles;
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, ListBookmarked())
                .RETURN(articles));
    }

    void setProjectArticles(const std::string& project, const std::vector<Arxiv::Article>& articles) {
        m_project_articles[project] = articles;
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetArticlesForProject(project))
                .RETURN(articles));
    }

    void setProjects(const std::vector<std::string>& projects) {
        m_projects = projects;
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetProjects())
                .RETURN(projects));
        // All projects are top-level by default
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetProjectParent(ANY(std::string)))
                .RETURN(std::string{}));
    }

    void setRatedArticles(const Arxiv::DatabaseManager::RatedArticleList& rated) {
        m_rated_articles = rated;
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetRatedArticles())
                .RETURN(m_rated_articles));
        for (const auto &[article, rating] : rated) {
            m_expectations.push_back(
                NAMED_ALLOW_CALL(*this, GetRating(article.link))
                    .RETURN(rating));
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
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, ToggleBookmark(article_link, is_bookmarked)));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, ListBookmarked())
                .RETURN(m_bookmarked_articles));
    }

private:
    std::vector<std::unique_ptr<trompeloeil::expectation>> m_expectations;
    std::vector<Arxiv::Article> m_articles;
    std::vector<Arxiv::Article> m_bookmarked_articles;
    std::vector<std::string> m_projects;
    std::unordered_map<std::string, std::vector<Arxiv::Article>> m_project_articles;
    Arxiv::DatabaseManager::RatedArticleList m_rated_articles;
};

} // namespace test
} // namespace arxiv_tui
