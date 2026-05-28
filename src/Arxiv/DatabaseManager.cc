// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/DatabaseManager.hh"

#include "Arxiv/Article.hh"
#include "Arxiv/Fetcher.hh"

#include <chrono>
#include <ctime>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <string_view>

#include "spdlog/spdlog.h"

using Arxiv::DatabaseManager;

// ---------------------------------------------------------------------------
// SQL helpers
// ---------------------------------------------------------------------------
namespace {

// The article column list appears in nearly every SELECT — keep it in one
// place so adding a column does not require touching seven query strings.
constexpr const char* ARTICLE_COLUMNS =
    "link, title, authors, abstract, date, bookmarked, category, is_replacement";
constexpr const char* ARTICLE_COLUMNS_A =
    "a.link, a.title, a.authors, a.abstract, a.date, a.bookmarked, a.category, a.is_replacement";

/// RAII wrapper around a prepared sqlite3_stmt. Construction prepares the
/// statement (throws on failure); destruction always finalizes.
///
/// The fluent `bind(idx, value)` overloads cover every type used in this file
/// and return *this so calls can be chained in expression-style code.
///
/// Use `step_done()` for INSERT/UPDATE/DELETE where SQLITE_DONE is expected
/// and any other return is a fatal error worth throwing about. Use
/// `for_each(on_row)` for SELECTs that may return many rows. Single-row
/// SELECTs can call `step()` directly and inspect the return value.
class Stmt {
  public:
    Stmt(sqlite3* db, const char* sql, std::string_view op)
        : m_db(db)
        , m_op(op) {
        if (sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("[Database]: ") + std::string(op) +
                                     " prepare failed: " + sqlite3_errmsg(db));
        }
    }
    ~Stmt() {
        if (m_stmt)
            sqlite3_finalize(m_stmt);
    }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& bind(int idx, const std::string& v) {
        sqlite3_bind_text(m_stmt, idx, v.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& bind(int idx, const char* v) {
        sqlite3_bind_text(m_stmt, idx, v, -1, SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& bind(int idx, int v) {
        sqlite3_bind_int(m_stmt, idx, v);
        return *this;
    }
    Stmt& bind(int idx, sqlite3_int64 v) {
        sqlite3_bind_int64(m_stmt, idx, v);
        return *this;
    }
    Stmt& bind(int idx, double v) {
        sqlite3_bind_double(m_stmt, idx, v);
        return *this;
    }

    int step() { return sqlite3_step(m_stmt); }

    /// Step a mutation, throwing if the result is anything other than DONE.
    void step_done() {
        if (sqlite3_step(m_stmt) != SQLITE_DONE) {
            throw std::runtime_error(std::string("[Database]: ") + std::string(m_op) +
                                     " failed: " + sqlite3_errmsg(m_db));
        }
    }

    /// Iterate every result row, invoking `on_row(stmt)` once per row.
    template <typename F> void for_each(F&& on_row) {
        while (sqlite3_step(m_stmt) == SQLITE_ROW)
            on_row(m_stmt);
    }

    sqlite3_stmt* raw() { return m_stmt; }

  private:
    sqlite3* m_db;
    sqlite3_stmt* m_stmt = nullptr;
    std::string_view m_op;
};

} // namespace

DatabaseManager::DatabaseManager(const std::string& path) {
    spdlog::info("[Database]: Opening database at {}", path);
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
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

    // Create article ratings table
    ExecuteSQL(R"(CREATE TABLE IF NOT EXISTS article_ratings (
               article_link TEXT PRIMARY KEY,
               rating INTEGER NOT NULL,
               FOREIGN KEY(article_link) REFERENCES articles(link)))");

    // Create project notes table
    ExecuteSQL(R"(CREATE TABLE IF NOT EXISTS project_notes (
               project_name TEXT,
               article_link TEXT,
               note TEXT,
               PRIMARY KEY (project_name, article_link),
               FOREIGN KEY(project_name) REFERENCES projects(name),
               FOREIGN KEY(article_link) REFERENCES articles(link)))");

    // Add parent column to projects table (migration for existing DBs)
    try {
        ExecuteSQL("ALTER TABLE projects ADD COLUMN parent TEXT DEFAULT ''");
    } catch (const std::exception&) {
        // Column already exists — ignore
    }

    // Add relevance_score column to articles table (migration for existing DBs)
    try {
        ExecuteSQL("ALTER TABLE articles ADD COLUMN relevance_score REAL DEFAULT 0.0");
    } catch (const std::exception&) {
        // Column already exists — ignore
    }

    // Add category column to articles table (migration for existing DBs)
    try {
        ExecuteSQL("ALTER TABLE articles ADD COLUMN category TEXT DEFAULT ''");
    } catch (const std::exception&) {
        // Column already exists — ignore
    }

    // Add is_replacement column (migration for existing DBs). Used by the
    // New Articles view to hide updates of older submissions.
    try {
        ExecuteSQL("ALTER TABLE articles ADD COLUMN is_replacement INTEGER DEFAULT 0");
    } catch (const std::exception&) {
        // Column already exists — ignore
    }

    // Create followed_authors table
    ExecuteSQL(R"(CREATE TABLE IF NOT EXISTS followed_authors (
               author_name TEXT PRIMARY KEY))");

    // Create metadata key-value table
    ExecuteSQL(R"(CREATE TABLE IF NOT EXISTS metadata (
               key   TEXT PRIMARY KEY,
               value TEXT NOT NULL DEFAULT ''))");

    spdlog::info("[Database]: Initialized");
}

DatabaseManager::~DatabaseManager() {
    spdlog::info("[Database]: Closing database");
    sqlite3_close(db);
}

void DatabaseManager::SetupTracing() {
    if (sqlite3_trace_v2(db,
                         SQLITE_TRACE_STMT | SQLITE_TRACE_PROFILE,
                         DatabaseManager::TraceCallback,
                         nullptr) != SQLITE_OK) {
        spdlog::error("[Database]: Failed to set up SQLite tracing: {}", sqlite3_errmsg(db));
    } else {
        spdlog::info("[Database]: SQLite tracing enabled");
    }
}

void DatabaseManager::ExecuteSQL(const std::string& sql) {
    char* errmsg;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string error_msg = errmsg;
        sqlite3_free(errmsg);
        throw std::runtime_error(error_msg);
    }
}

void DatabaseManager::AddArticles(const std::vector<Article>& articles) {
    if (articles.empty())
        return;
    ExecuteSQL("BEGIN TRANSACTION");
    try {
        for (const auto& a : articles)
            AddArticle(a);
        ExecuteSQL("COMMIT");
    } catch (...) {
        ExecuteSQL("ROLLBACK");
        throw;
    }
}

void DatabaseManager::AddArticle(const Article& article) {
    spdlog::debug("[Database]: Adding article: {}", article.link);
    auto timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(article.date.time_since_epoch()).count();

    Stmt stmt(db,
              "INSERT OR REPLACE INTO articles "
              "(link, title, authors, abstract, date, bookmarked, category, is_replacement) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
              "AddArticle");
    stmt.bind(1, article.link)
        .bind(2, article.title)
        .bind(3, article.authors)
        .bind(4, article.abstract)
        .bind(5, static_cast<sqlite3_int64>(timestamp))
        .bind(6, article.bookmarked ? 1 : 0)
        .bind(7, article.category)
        .bind(8, article.is_replacement ? 1 : 0);
    stmt.step_done();
}

std::vector<Arxiv::Article> DatabaseManager::GetRecent(int days) {
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS + " FROM articles";
    if (days >= 0) {
        auto now = std::chrono::system_clock::now();
        auto past = now - std::chrono::hours(24 * days);
        auto past_seconds =
            std::chrono::duration_cast<std::chrono::seconds>(past.time_since_epoch()).count();
        sql += " WHERE date >= " + std::to_string(past_seconds);
    }
    sql += " ORDER BY date DESC";

    std::vector<Article> articles;
    Stmt stmt(db, sql.c_str(), "GetRecent");
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    return articles;
}

std::vector<Arxiv::Article> DatabaseManager::ListBookmarked() {
    spdlog::debug("[Database]: Collecting all bookmarked articles");
    std::string sql =
        std::string("SELECT ") + ARTICLE_COLUMNS + " FROM articles WHERE bookmarked = 1";
    std::vector<Article> articles;
    Stmt stmt(db, sql.c_str(), "ListBookmarked");
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    return articles;
}

void DatabaseManager::ToggleBookmark(const std::string& link, bool bookmarked) {
    spdlog::debug("[Database]: Toggling bookmark for {}", link);
    Stmt stmt(db, "UPDATE articles SET bookmarked = ? WHERE link = ?", "ToggleBookmark");
    stmt.bind(1, bookmarked ? 1 : 0).bind(2, link).step_done();
}

void DatabaseManager::AddProject(const std::string& project_name) {
    Stmt stmt(db, "INSERT OR REPLACE INTO projects (name) VALUES (?)", "AddProject");
    stmt.bind(1, project_name).step_done();
}

void DatabaseManager::RemoveProject(const std::string& project_name) {
    ExecuteSQL("BEGIN TRANSACTION");
    auto bind_and_step = [&](const char* sql, const char* op) {
        try {
            Stmt s(db, sql, op);
            s.bind(1, project_name).step_done();
        } catch (...) {
            ExecuteSQL("ROLLBACK");
            throw;
        }
    };
    bind_and_step("DELETE FROM projects WHERE name = ?", "RemoveProject:projects");
    bind_and_step("DELETE FROM project_articles WHERE project_name = ?",
                  "RemoveProject:project_articles");
    bind_and_step("DELETE FROM project_notes WHERE project_name = ?",
                  "RemoveProject:project_notes");
    bind_and_step("UPDATE projects SET parent = '' WHERE parent = ?", "RemoveProject:reparent");
    ExecuteSQL("COMMIT");
}

std::vector<std::string> DatabaseManager::GetProjects() {
    std::vector<std::string> projects;
    Stmt stmt(db, "SELECT name FROM projects", "GetProjects");
    stmt.for_each([&](sqlite3_stmt* s) {
        projects.push_back(reinterpret_cast<const char*>(sqlite3_column_text(s, 0)));
    });
    return projects;
}

void DatabaseManager::LinkArticleToProject(const std::string& article_link,
                                           const std::string& project_name) {
    spdlog::debug("[Database]: Linking article {} to project {}", article_link, project_name);
    Stmt stmt(db,
              "INSERT OR IGNORE INTO project_articles (project_name, article_link) VALUES (?, ?)",
              "LinkArticleToProject");
    stmt.bind(1, project_name).bind(2, article_link).step_done();
}

void DatabaseManager::UnlinkArticleFromProject(const std::string& article_link,
                                               const std::string& project_name) {
    spdlog::debug("[Database]: Unlinking article {} from project {}", article_link, project_name);
    Stmt stmt(db,
              "DELETE FROM project_articles WHERE project_name = ? AND article_link = ?",
              "UnlinkArticleFromProject");
    stmt.bind(1, project_name).bind(2, article_link).step_done();
}

std::vector<Arxiv::Article>
DatabaseManager::GetArticlesForProject(const std::string& project_name) {
    spdlog::debug("[Database]: Collecting articles for project {}", project_name);
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS_A +
                      " FROM articles a JOIN project_articles pa ON a.link = pa.article_link "
                      "WHERE pa.project_name = ?";
    std::vector<Article> articles;
    Stmt stmt(db, sql.c_str(), "GetArticlesForProject");
    stmt.bind(1, project_name);
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    spdlog::debug("[Database]: Success. Found {} articles", articles.size());
    return articles;
}

const char* DatabaseManager::ExtractColumn(sqlite3_stmt* stmt, int index) {
    const unsigned char* text = sqlite3_column_text(stmt, index);
    return text ? reinterpret_cast<const char*>(text) : "";
}

Arxiv::Article DatabaseManager::RowToArticle(sqlite3_stmt* stmt) {
    Article article;
    article.link = ExtractColumn(stmt, 0);
    article.title = ExtractColumn(stmt, 1);
    article.authors = ExtractColumn(stmt, 2);
    article.abstract = ExtractColumn(stmt, 3);

    int64_t timestamp = sqlite3_column_int64(stmt, 4);
    article.date = std::chrono::system_clock::from_time_t(timestamp);

    article.bookmarked = sqlite3_column_int(stmt, 5) != 0;
    article.category = ExtractColumn(stmt, 6);
    article.is_replacement = sqlite3_column_int(stmt, 7) != 0;

    return article;
}

int DatabaseManager::TraceCallback(unsigned type, void*, void* p, void* x) {
    if (type == SQLITE_TRACE_STMT) {
        auto* stmt = static_cast<sqlite3_stmt*>(p);
        if (stmt) {
            const char* sql = sqlite3_sql(stmt);
            if (sql) {
                spdlog::trace("[Database]: SQL Executed: {}", sql);
            }
        }
    } else if (type == SQLITE_TRACE_PROFILE) {
        auto* stmt = static_cast<sqlite3_stmt*>(p);
        auto time = *static_cast<sqlite3_int64*>(x);
        if (stmt) {
            const char* sql = sqlite3_sql(stmt);
            if (sql) {
                spdlog::trace("[Database]: SQL Profile: {} ({} us)", sql, time);
            }
        }
    }
    return 0;
}

std::vector<std::string> DatabaseManager::GetProjectsForArticle(const std::string& article_link) {
    spdlog::debug("[Database]: Getting projects for article {}", article_link);
    std::vector<std::string> projects;
    Stmt stmt(db,
              "SELECT project_name FROM project_articles WHERE article_link = ?",
              "GetProjectsForArticle");
    stmt.bind(1, article_link);
    stmt.for_each([&](sqlite3_stmt* s) {
        projects.push_back(reinterpret_cast<const char*>(sqlite3_column_text(s, 0)));
    });
    return projects;
}

std::vector<Arxiv::Article> DatabaseManager::GetArticlesForDateRange(const std::string& start_date,
                                                                     const std::string& end_date) {
    std::vector<Article> articles;
    std::tm start_tm = {};
    std::tm end_tm = {};

    if (strptime(start_date.c_str(), "%Y-%m-%d", &start_tm) == nullptr) {
        spdlog::error("[Database]: Invalid start date format: {}", start_date);
        return articles;
    }
    if (strptime(end_date.c_str(), "%Y-%m-%d", &end_tm) == nullptr) {
        spdlog::error("[Database]: Invalid end date format: {}", end_date);
        return articles;
    }

    time_t start_time = mktime(&start_tm);
    time_t end_time = mktime(&end_tm) + 24 * 60 * 60;

    spdlog::debug("[Database]: Fetching articles between {} and {}", start_date, end_date);

    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS +
                      " FROM articles WHERE date >= ? AND date < ? ORDER BY date DESC";
    Stmt stmt(db, sql.c_str(), "GetArticlesForDateRange");
    stmt.bind(1, static_cast<sqlite3_int64>(start_time))
        .bind(2, static_cast<sqlite3_int64>(end_time));
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });

    spdlog::debug("[Database]: Found {} articles in date range", articles.size());
    return articles;
}

void DatabaseManager::SetRating(const std::string& link, int rating) {
    spdlog::debug("[Database]: Setting rating {} for {}", rating, link);
    Stmt stmt(db,
              "INSERT OR REPLACE INTO article_ratings (article_link, rating) VALUES (?, ?)",
              "SetRating");
    stmt.bind(1, link).bind(2, rating).step_done();
}

int DatabaseManager::GetRating(const std::string& link) {
    Stmt stmt(db, "SELECT rating FROM article_ratings WHERE article_link = ?", "GetRating");
    stmt.bind(1, link);
    int rating = 0;
    if (stmt.step() == SQLITE_ROW) {
        rating = sqlite3_column_int(stmt.raw(), 0);
    }
    return rating;
}

DatabaseManager::RatedArticleList DatabaseManager::GetRatedArticles() {
    RatedArticleList result;
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS_A +
                      ", r.rating "
                      "FROM articles a JOIN article_ratings r ON a.link = r.article_link";
    Stmt stmt(db, sql.c_str(), "GetRatedArticles");
    stmt.for_each([&](sqlite3_stmt* s) {
        Article article = RowToArticle(s);
        // ARTICLE_COLUMNS_A has 8 columns; rating is column 8 (index 8).
        int rating = sqlite3_column_int(s, 8);
        result.emplace_back(std::move(article), rating);
    });
    spdlog::debug("[Database]: Found {} rated articles", result.size());
    return result;
}

std::string DatabaseManager::GetProjectParent(const std::string& project_name) {
    Stmt stmt(db, "SELECT COALESCE(parent, '') FROM projects WHERE name = ?", "GetProjectParent");
    stmt.bind(1, project_name);
    std::string parent;
    if (stmt.step() == SQLITE_ROW) {
        parent = ExtractColumn(stmt.raw(), 0);
    }
    return parent;
}

void DatabaseManager::SetProjectParent(const std::string& project_name, const std::string& parent) {
    Stmt stmt(db, "UPDATE projects SET parent = ? WHERE name = ?", "SetProjectParent");
    stmt.bind(1, parent).bind(2, project_name).step_done();
}

void DatabaseManager::SetProjectNote(const std::string& project_name,
                                     const std::string& article_link,
                                     const std::string& note) {
    spdlog::debug(
        "[Database]: Setting note for article {} in project {}", article_link, project_name);
    Stmt stmt(
        db,
        "INSERT OR REPLACE INTO project_notes (project_name, article_link, note) VALUES (?, ?, ?)",
        "SetProjectNote");
    stmt.bind(1, project_name).bind(2, article_link).bind(3, note).step_done();
}

std::string DatabaseManager::GetProjectNote(const std::string& project_name,
                                            const std::string& article_link) {
    Stmt stmt(db,
              "SELECT note FROM project_notes WHERE project_name = ? AND article_link = ?",
              "GetProjectNote");
    stmt.bind(1, project_name).bind(2, article_link);
    std::string note;
    if (stmt.step() == SQLITE_ROW) {
        note = ExtractColumn(stmt.raw(), 0);
    }
    return note;
}

std::vector<Arxiv::Article> DatabaseManager::SearchArticles(const std::string& query,
                                                            bool search_title,
                                                            bool search_authors,
                                                            bool search_abstract) {
    std::vector<Article> articles;

    std::vector<std::string> conditions;
    if (search_title)
        conditions.push_back("title LIKE ? ESCAPE '\\'");
    if (search_authors)
        conditions.push_back("authors LIKE ? ESCAPE '\\'");
    if (search_abstract)
        conditions.push_back("abstract LIKE ? ESCAPE '\\'");

    if (conditions.empty()) {
        spdlog::warn("[Database]: No search fields selected");
        return articles;
    }

    std::string where_clause = "WHERE " + conditions[0];
    for (size_t i = 1; i < conditions.size(); ++i)
        where_clause += " OR " + conditions[i];

    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS + " FROM articles " + where_clause +
                      " ORDER BY date DESC";

    spdlog::debug("[Database]: Searching for '{}' in title: {}, authors: {}, abstract: {}",
                  query,
                  search_title,
                  search_authors,
                  search_abstract);

    // Escape LIKE metacharacters in the query itself
    std::string escaped_query;
    escaped_query.reserve(query.size() + 4);
    for (char c : query) {
        if (c == '%' || c == '_' || c == '\\')
            escaped_query += '\\';
        escaped_query += c;
    }
    std::string pattern = "%" + escaped_query + "%";

    Stmt stmt(db, sql.c_str(), "SearchArticles");
    int param = 1;
    if (search_title)
        stmt.bind(param++, pattern);
    if (search_authors)
        stmt.bind(param++, pattern);
    if (search_abstract)
        stmt.bind(param++, pattern);
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });

    spdlog::debug("[Database]: Found {} articles matching search criteria", articles.size());
    return articles;
}

void DatabaseManager::SetRelevanceScore(const std::string& link, float score) {
    Stmt stmt(db, "UPDATE articles SET relevance_score = ? WHERE link = ?", "SetRelevanceScore");
    stmt.bind(1, static_cast<double>(score)).bind(2, link).step_done();
}

float DatabaseManager::GetRelevanceScore(const std::string& link) {
    Stmt stmt(db, "SELECT relevance_score FROM articles WHERE link = ?", "GetRelevanceScore");
    stmt.bind(1, link);
    float score = 0.0f;
    if (stmt.step() == SQLITE_ROW) {
        score = static_cast<float>(sqlite3_column_double(stmt.raw(), 0));
    }
    return score;
}

void DatabaseManager::FollowAuthor(const std::string& author_name) {
    Stmt stmt(
        db, "INSERT OR IGNORE INTO followed_authors (author_name) VALUES (?)", "FollowAuthor");
    stmt.bind(1, author_name).step();
}

void DatabaseManager::UnfollowAuthor(const std::string& author_name) {
    Stmt stmt(db, "DELETE FROM followed_authors WHERE author_name = ?", "UnfollowAuthor");
    stmt.bind(1, author_name).step();
}

bool DatabaseManager::IsFollowingAuthor(const std::string& author_name) {
    Stmt stmt(db, "SELECT 1 FROM followed_authors WHERE author_name = ?", "IsFollowingAuthor");
    stmt.bind(1, author_name);
    return stmt.step() == SQLITE_ROW;
}

std::vector<std::string> DatabaseManager::GetFollowedAuthors() {
    std::vector<std::string> authors;
    Stmt stmt(db, "SELECT author_name FROM followed_authors", "GetFollowedAuthors");
    stmt.for_each([&](sqlite3_stmt* s) {
        authors.push_back(reinterpret_cast<const char*>(sqlite3_column_text(s, 0)));
    });
    return authors;
}

void DatabaseManager::SetMetadata(const std::string& key, const std::string& value) {
    Stmt stmt(db, "INSERT OR REPLACE INTO metadata (key, value) VALUES (?, ?)", "SetMetadata");
    stmt.bind(1, key).bind(2, value).step();
}

std::string DatabaseManager::GetMetadata(const std::string& key) {
    Stmt stmt(db, "SELECT value FROM metadata WHERE key = ?", "GetMetadata");
    stmt.bind(1, key);
    std::string result;
    if (stmt.step() == SQLITE_ROW) {
        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt.raw(), 0));
        if (val)
            result = val;
    }
    return result;
}

std::vector<Arxiv::Article> DatabaseManager::GetArticlesSince(const std::string& utc_date) {
    // Convert "YYYY-MM-DD" to UTC midnight Unix timestamp.
    std::tm tm{};
    tm.tm_year = std::stoi(utc_date.substr(0, 4)) - 1900;
    tm.tm_mon = std::stoi(utc_date.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(utc_date.substr(8, 2));
    std::time_t since_ts = timegm(&tm);

    std::vector<Arxiv::Article> articles;
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS +
                      " FROM articles WHERE date >= ? ORDER BY date DESC";
    Stmt stmt(db, sql.c_str(), "GetArticlesSince");
    stmt.bind(1, static_cast<sqlite3_int64>(since_ts));
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    return articles;
}
