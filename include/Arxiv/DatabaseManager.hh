#ifndef ARXIV_DATABASE_MANAGER
#define ARXIV_DATABASE_MANAGER

#include <sqlite3.h>
#include <string>
#include <vector>

namespace Arxiv {

class Article;

class DatabaseManager {
  public:
    explicit DatabaseManager(const std::string &path);
    virtual ~DatabaseManager();

    // Article management
    virtual void AddArticle(const Article &article);
    virtual std::vector<Article> GetRecent(int days);
    virtual std::vector<Article> ListBookmarked();
    virtual std::vector<Article> GetArticlesForProject(const std::string &project_name);
    virtual void ToggleBookmark(const std::string &link, bool bookmarked=true);

    // Project management
    virtual void AddProject(const std::string &project_name);
    virtual void RemoveProject(const std::string &project_name);
    virtual std::vector<std::string> GetProjects();
    virtual void LinkArticleToProject(const std::string &article_link, const std::string &project_name);
    virtual void UnlinkArticleFromProject(const std::string &article_link, const std::string &project_name);
    virtual std::vector<std::string> GetProjectsForArticle(const std::string &article_link);

  private:
    sqlite3 *db;

    void SetupTracing();
    void ExecuteSQL(const std::string &sql);
    std::string EscapeString(const std::string &str);
    void Query(const std::string &query);
    Article RowToArticle(sqlite3_stmt *stmt);
    const char* ExtractColumn(sqlite3_stmt *stmt, int index);

    static int TraceCallback(unsigned type, void *, void *p, void *);
};

}

#endif
