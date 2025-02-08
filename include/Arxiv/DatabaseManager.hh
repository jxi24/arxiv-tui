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

    void AddArticle(const Article &article);
    void ToggleBookmark(const std::string &link, bool bookmarked=true);
    std::vector<Article> GetRecent(int days);
    std::vector<Article> ListBookmarked();

  private:
    sqlite3 *db;
    sqlite3_stmt *insert_stmt = nullptr;
    sqlite3_stmt *bookmark_stmt = nullptr;
    sqlite3_stmt *get_all_stmt = nullptr;
    sqlite3_stmt *get_recent_stmt = nullptr;
    sqlite3_stmt *get_bookmarks_stmt = nullptr;

    std::string m_path;

    void Initialize();
    void PrepareStatements();
    void Query(const std::string &query);
    Article RowToArticle(sqlite3_stmt *stmt);
    static int TraceCallback(unsigned type, void *, void *p, void *);

    void ValidateAndStep(sqlite3_stmt *stmt, int rc);
    const char* ExtractColumn(sqlite3_stmt *stmt, int index);
};

}

#endif
