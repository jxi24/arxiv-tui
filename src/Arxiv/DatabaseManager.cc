#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "fmt/core.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include <sqlite3.h>
#include <stdexcept>

using Arxiv::DatabaseManager;

DatabaseManager::DatabaseManager(const std::string &path) { 
    spdlog::info("[Database]: Opening database at {}", path);
    if(sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        throw std::runtime_error("[Database]: Can't open database: " +
                                 std::string(sqlite3_errmsg(db)));
    }
    sqlite3_trace_v2(db, SQLITE_TRACE_STMT, DatabaseManager::TraceCallback, nullptr);

    // Create articles table if it doesn't exist
    ExecuteSQL(R"(CREATE TABLE IF NOT EXISTS articles (
               link TEXT PRIMARY KEY,
               title TEXT,
               authors TEXT,
               abstract TEXT,
               date INTEGER,
               bookmarked INTEGER DEFAULT 0)
        )");

    // Create projects table
    ExecuteSQL(R"(CREATE TABLE IF NOT EXISTS projects (
               name TEXT PRIMARY KEY))");

    // Create project-article link table
    ExecuteSQL(R"(CREATE TABLE IF NOT EXISTS project_articles (
               project_name TEXT,
               article_link TEXT,
               FOREIGN KEY(project_name) REFERENCES projects(name),
               FOREIGN KEY(article_link) REFERENCES articles(link)
               PRIMARY KEY (project_name, article_link)))");
    spdlog::info("[Database]: Initialized");
}

DatabaseManager::~DatabaseManager() {
    spdlog::info("[Database]: Closing database");
    sqlite3_close(db);
}

void DatabaseManager::SetupTracing() {
    if(sqlite3_trace_v2(db, SQLITE_TRACE_STMT | SQLITE_TRACE_PROFILE, DatabaseManager::TraceCallback, nullptr) != SQLITE_OK) {
        spdlog::error("[Database]: Failed to set up SQLite tracing: {}", sqlite3_errmsg(db));
    } else {
        spdlog::info("[Database]: SQLite tracing enabled");
    }
}

void DatabaseManager::ExecuteSQL(const std::string &sql) {
    char *errmsg;
    if(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string error_msg = errmsg;
        sqlite3_free(errmsg);
        throw std::runtime_error(error_msg);
    }
}

std::string DatabaseManager::EscapeString(const std::string &str) {
    std::string escaped_str = str;
    size_t pos = 0;
    while((pos = escaped_str.find('\'', pos)) != std::string::npos) {
        escaped_str.insert(pos, "'");
        pos += 2;
    }
    pos = 0;
    while((pos = escaped_str.find('{', pos)) != std::string::npos) {
        escaped_str.insert(pos, "{");
        pos += 2;
    }
    pos = 0;
    while((pos = escaped_str.find('}', pos)) != std::string::npos) {
        escaped_str.insert(pos, "}");
        pos += 2;
    }
    return escaped_str;
}

void DatabaseManager::AddArticle(const Article &article) {
    spdlog::debug("[Database]: Adding article: {}", article.link);
    // Convert time_point to Unix timestamp
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        article.date.time_since_epoch()).count();
    std::string sql = fmt::format("INSERT OR REPLACE INTO articles (link, title, authors, abstract, date, bookmarked) VALUES ('{}', '{}', '{}', '{}', {}, {})",
                                  EscapeString(article.link),
                                  EscapeString(article.title),
                                  EscapeString(article.authors),
                                  EscapeString(article.abstract),
                                  timestamp, article.bookmarked);
    ExecuteSQL(sql);
}

std::vector<Arxiv::Article> DatabaseManager::GetRecent(int days) {
    std::vector<Article> articles;
    sqlite3_stmt *stmt;
    std::string sql = "SELECT link, title, authors, abstract, date, bookmarked FROM articles";
    if(days >= 0) {
        auto now = std::chrono::system_clock::now();
        auto past = now - std::chrono::hours(24 * days);
        auto past_seconds = std::chrono::duration_cast<std::chrono::seconds>(past.time_since_epoch()).count();
        sql += " WHERE date >= " + std::to_string(past_seconds);
    }
    sql += " ORDER BY date DESC";
    if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            articles.push_back(RowToArticle(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return articles;
}

std::vector<Arxiv::Article> DatabaseManager::ListBookmarked() {
    std::vector<Article> articles;
    spdlog::debug("[Database]: Collecting all bookmarked articles");
    sqlite3_stmt *stmt;
    std::string sql = "SELECT link, title, authors, abstract, date, bookmarked FROM articles WHERE bookmarked = 1";
    if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            articles.push_back(RowToArticle(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return articles;
}

void DatabaseManager::ToggleBookmark(const std::string &link, bool bookmarked) {
    spdlog::debug("[Database]: Toggling bookmark for {}", link);
    std::string sql = fmt::format("UPDATE articles SET bookmarked = {} WHERE link = '{}'",
                                  bookmarked, link);
    ExecuteSQL(sql);
}

void DatabaseManager::AddProject(const std::string &project_name) {
    std::string sql = fmt::format("INSERT OR REPLACE INTO projects (name) VALUES ('{}')",
                                  project_name);
    ExecuteSQL(sql);
}

void DatabaseManager::RemoveProject(const std::string &project_name) {
    std::string sql1 = fmt::format("DELETE FROM projects WHERE name = '{}'", project_name);
    std::string sql2 = fmt::format("DELETE FROM project_articles WHERE project_name = '{}'", project_name);
    ExecuteSQL("BEGIN TRANSACTION");
    ExecuteSQL(sql1);
    ExecuteSQL(sql2);
    ExecuteSQL("COMMIT");
}

std::vector<std::string> DatabaseManager::GetProjects() {
    std::vector<std::string> projects;
    sqlite3_stmt *stmt;
    std::string sql = "SELECT name FROM projects";
    if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            projects.push_back(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }
    return projects;
}

void DatabaseManager::LinkArticleToProject(const std::string &article_link, const std::string &project_name) {
    std::string sql = fmt::format("INSERT OR IGNORE INTO project_articles (project_name, article_link) VALUES ('{}', '{}')",
                                  project_name, article_link);
    spdlog::debug("[Database]: Linking article {} to project {}", article_link, project_name);
    ExecuteSQL(sql);
}

void DatabaseManager::UnlinkArticleFromProject(const std::string &article_link, const std::string &project_name) {
    std::string sql = fmt::format("DELETE FROM project_articles WHERE project_name = '{}' AND article_link = '{}'",
                                  project_name, article_link);
    spdlog::debug("[Database]: Unlinking article {} to project {}", article_link, project_name);
    ExecuteSQL(sql);
}

std::vector<Arxiv::Article> DatabaseManager::GetArticlesForProject(const std::string &project_name) {
    std::vector<Article> articles;
    sqlite3_stmt *stmt;
    std::string sql = fmt::format(R"(SELECT a.link, a.title, a.authors, a.abstract, a.date, a.bookmarked FROM articles a
                                  JOIN project_articles pa ON a.link = pa.article_link
                                  WHERE pa.project_name = '{}')",
                                  project_name);
    spdlog::debug("[Database]: Collecting articles for project {}", project_name);

    if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            articles.push_back(RowToArticle(stmt));
        }
        sqlite3_finalize(stmt);
    }
    spdlog::debug("[Database]: Success. Found {} articles", articles.size());
    return articles;
}

const char* DatabaseManager::ExtractColumn(sqlite3_stmt *stmt, int index) {
    const unsigned char *text = sqlite3_column_text(stmt, index);
    return text ? reinterpret_cast<const char*>(text) : "";
}

Arxiv::Article DatabaseManager::RowToArticle(sqlite3_stmt *stmt) {
    Article article;
    article.link = ExtractColumn(stmt,0);
    article.title = ExtractColumn(stmt, 1);
    article.authors = ExtractColumn(stmt,2);
    article.abstract = ExtractColumn(stmt,3);
    // article.category = ExtractColumn(stmt,4);

    // Convert Unix timestamp back to time_point
    int64_t timestamp = sqlite3_column_int64(stmt, 4);
    article.date = std::chrono::system_clock::from_time_t(timestamp);

    article.bookmarked = sqlite3_column_int(stmt, 5) != 0;

    return article;
}

int DatabaseManager::TraceCallback(unsigned type, void *, void *p, void *x) {
    if(type == SQLITE_TRACE_STMT) {
        auto *stmt = static_cast<sqlite3_stmt*>(p);
        if(stmt) {
            const char *sql = sqlite3_sql(stmt);
            if(sql) {
                spdlog::trace("[Database]: SQL Executed: {}", sql);
            }
        }
    } else if(type == SQLITE_TRACE_PROFILE) {
        auto *stmt = static_cast<sqlite3_stmt*>(p);
        auto time = *static_cast<sqlite3_int64*>(x);
        if(stmt) {
            const char *sql = sqlite3_sql(stmt);
            if(sql) {
                spdlog::trace("[Database]: SQL Profile: {} ({} us)", sql, time);
            }
        }
    }
    return 0;
}
