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
    explicit DatabaseManager(const std::string &path);
    virtual ~DatabaseManager();

    // Article management
    virtual void AddArticle(const Article &article);
    virtual std::vector<Article> GetRecent(int days);
    virtual std::vector<Article> ListBookmarked();
    virtual std::vector<Article> GetArticlesForProject(const std::string &project_name);
    virtual std::vector<Article> GetArticlesForDateRange(const std::string &start_date, const std::string &end_date);
    virtual std::vector<Article> SearchArticles(const std::string &query, bool search_title = true, 
                                              bool search_authors = true, bool search_abstract = true);
    virtual void ToggleBookmark(const std::string &link, bool bookmarked=true);

    // Rating management
    virtual void SetRating(const std::string &link, int rating);
    virtual int GetRating(const std::string &link);
    virtual RatedArticleList GetRatedArticles();

    // Project management
    virtual void AddProject(const std::string &project_name);
    virtual void RemoveProject(const std::string &project_name);
    virtual std::vector<std::string> GetProjects();
    virtual std::string GetProjectParent(const std::string &project_name);
    virtual void SetProjectParent(const std::string &project_name, const std::string &parent);
    virtual void LinkArticleToProject(const std::string &article_link, const std::string &project_name);
    virtual void UnlinkArticleFromProject(const std::string &article_link, const std::string &project_name);
    virtual std::vector<std::string> GetProjectsForArticle(const std::string &article_link);

    // Project notes
    virtual void SetProjectNote(const std::string &project_name, const std::string &article_link, const std::string &note);
    virtual std::string GetProjectNote(const std::string &project_name, const std::string &article_link);

  private:
    sqlite3 *db;

    void SetupTracing();
    void ExecuteSQL(const std::string &sql);
    void Query(const std::string &query);
    Article RowToArticle(sqlite3_stmt *stmt);
    const char* ExtractColumn(sqlite3_stmt *stmt, int index);

    static int TraceCallback(unsigned type, void *, void *p, void *);
};

}

#endif
