// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARXIV_DATABASE_MANAGER
#define ARXIV_DATABASE_MANAGER

#include <sqlite3.h>
#include <string>
#include <vector>

namespace Arxiv {

class Article;

class DatabaseManager {
  public:
    // Type alias to avoid comma issues when used in trompeloeil MAKE_MOCK macros
    using RatedArticleList = std::vector<std::pair<Article, int>>;
    explicit DatabaseManager(const std::string& path);
    virtual ~DatabaseManager();

    // Article management
    virtual void AddArticle(const Article& article);
    virtual std::vector<Article> GetRecent(int days);
    virtual std::vector<Article> ListBookmarked();
    virtual std::vector<Article> GetArticlesForProject(const std::string& project_name);
    virtual std::vector<Article> GetArticlesForDateRange(const std::string& start_date,
                                                         const std::string& end_date);
    virtual std::vector<Article> SearchArticles(const std::string& query,
                                                bool search_title = true,
                                                bool search_authors = true,
                                                bool search_abstract = true);
    virtual void ToggleBookmark(const std::string& link, bool bookmarked = true);
    virtual void DeleteArticle(const std::string& link);

    // Rating management
    virtual void SetRating(const std::string& link, int rating);
    virtual int GetRating(const std::string& link);
    virtual RatedArticleList GetRatedArticles();

    // Relevance score cache (keyword / ML blend)
    virtual void SetRelevanceScore(const std::string& link, float score);
    virtual float GetRelevanceScore(const std::string& link);

    // Author subscriptions
    virtual void FollowAuthor(const std::string& author_name);
    virtual void UnfollowAuthor(const std::string& author_name);
    virtual bool IsFollowingAuthor(const std::string& author_name);
    virtual std::vector<std::string> GetFollowedAuthors();

    // Project management
    virtual void AddProject(const std::string& project_name);
    virtual void RemoveProject(const std::string& project_name);
    virtual std::vector<std::string> GetProjects();
    virtual std::string GetProjectParent(const std::string& project_name);
    virtual void SetProjectParent(const std::string& project_name, const std::string& parent);
    virtual void LinkArticleToProject(const std::string& article_link,
                                      const std::string& project_name);
    virtual void UnlinkArticleFromProject(const std::string& article_link,
                                          const std::string& project_name);
    virtual std::vector<std::string> GetProjectsForArticle(const std::string& article_link);

    // Project notes
    virtual void SetProjectNote(const std::string& project_name,
                                const std::string& article_link,
                                const std::string& note);
    virtual std::string GetProjectNote(const std::string& project_name,
                                       const std::string& article_link);

    // Application metadata (persistent key-value store)
    virtual void SetMetadata(const std::string& key, const std::string& value);
    virtual std::string GetMetadata(const std::string& key);

    // Articles submitted on or after the given UTC date ("YYYY-MM-DD")
    virtual std::vector<Article> GetArticlesSince(const std::string& utc_date);

    // Bulk insert wrapped in a single SQLite transaction. Hundreds of inserts
    // commit in milliseconds instead of seconds, which keeps the UI thread
    // from being blocked on DB reads while the background fetch is writing.
    virtual void AddArticles(const std::vector<Article>& articles);

  private:
    sqlite3* db;

    void SetupTracing();
    void ExecuteSQL(const std::string& sql);
    void Query(const std::string& query);
    Article RowToArticle(sqlite3_stmt* stmt);
    const char* ExtractColumn(sqlite3_stmt* stmt, int index);
    void MigrateNormalizeLinks();

    static int TraceCallback(unsigned type, void*, void* p, void*);
};

} // namespace Arxiv

#endif
