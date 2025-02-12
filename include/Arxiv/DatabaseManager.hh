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
    ~DatabaseManager();

    // Article management
    void AddArticle(const Article &article);
    std::vector<Article> GetRecent(int days);
    std::vector<Article> ListBookmarked();
    std::vector<Article> GetArticlesForProject(const std::string &project_name);
    void ToggleBookmark(const std::string &link, bool bookmarked=true);

    // Project management
    void AddProject(const std::string &project_name);
    void RemoveProject(const std::string &project_name);
    std::vector<std::string> GetProjects();
    void LinkArticleToProject(const std::string &article_link, const std::string &project_name);
    void UnlinkArticleFromProject(const std::string &article_link, const std::string &project_name);

  private:
    sqlite3 *db;

    void ExecuteSQL(const std::string &sql);
    std::string EscapeString(const std::string &str);
    void Query(const std::string &query);
    Article RowToArticle(sqlite3_stmt *stmt);
    const char* ExtractColumn(sqlite3_stmt *stmt, int index);

    static int TraceCallback(unsigned type, void *, void *p, void *);
};

}

#endif
