#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "spdlog/spdlog.h"
#include <chrono>
#include <sqlite3.h>
#include <stdexcept>

using Arxiv::DatabaseManager;

DatabaseManager::DatabaseManager(const std::string &path) : m_path{path} {
    spdlog::info("[Database]: Opening database at {}", path);
    int rc = sqlite3_open(path.c_str(), &db);
    if(rc) {
        throw std::runtime_error("[Database]: Can't open database: " +
                                 std::string(sqlite3_errmsg(db)));
    }

    sqlite3_trace_v2(db, SQLITE_TRACE_STMT, DatabaseManager::TraceCallback, nullptr);

    Initialize();
    PrepareStatements();
}

DatabaseManager::~DatabaseManager() {
    spdlog::info("[Database]: Closing database");
    // Finalize all prepared statements
    if(insert_stmt) sqlite3_finalize(insert_stmt);
    if(bookmark_stmt) sqlite3_finalize(bookmark_stmt);
    if(get_recent_stmt) sqlite3_finalize(get_recent_stmt);
    if(get_bookmarks_stmt) sqlite3_finalize(get_bookmarks_stmt);

    sqlite3_close(db);
}

void DatabaseManager::ValidateAndStep(sqlite3_stmt *stmt, int rc) {
    if(rc != SQLITE_OK) {
        spdlog::error("[Database]: Failed to bind parameters: {}", sqlite3_errmsg(db));
        return;
    }

    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE) {
        spdlog::error("[Database]: Failed to insert article: {}", sqlite3_errmsg(db));
    }

    sqlite3_reset(stmt);
}

void DatabaseManager::AddArticle(const Article &article) {
    spdlog::debug("[Database]: Adding article: {}", article.link);
    int rc = sqlite3_bind_text(insert_stmt, 1, article.title.c_str(), -1, SQLITE_STATIC);
    rc |= sqlite3_bind_text(insert_stmt, 2, article.link.c_str(), -1, SQLITE_STATIC);
    rc |= sqlite3_bind_text(insert_stmt, 3, article.abstract.c_str(), -1, SQLITE_STATIC);
    rc |= sqlite3_bind_text(insert_stmt, 4, article.category.c_str(), -1, SQLITE_STATIC);
    rc |= sqlite3_bind_text(insert_stmt, 5, article.authors.c_str(), -1, SQLITE_STATIC);

    // Convert time_point to Unix timestamp
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        article.date.time_since_epoch()).count();
    rc |= sqlite3_bind_int64(insert_stmt, 6, timestamp);
    ValidateAndStep(insert_stmt, rc);
}

void DatabaseManager::ToggleBookmark(const std::string &link, bool bookmarked) {
    spdlog::debug("[Database]: Toggling bookmark for {}", link);
    int rc = sqlite3_bind_int(bookmark_stmt, 1, bookmarked ? 1 : 0);
    rc |= sqlite3_bind_text(bookmark_stmt, 2, link.c_str(), -1, SQLITE_STATIC);
    ValidateAndStep(bookmark_stmt, rc);
}

std::vector<Arxiv::Article> DatabaseManager::GetRecent(int days) {
    std::vector<Article> articles;
    int rc = 0;
    if(days < 0) {
        spdlog::debug("[Database]: Getting all articles");
        while((rc = sqlite3_step(get_all_stmt)) == SQLITE_ROW) {
            articles.push_back(RowToArticle(get_all_stmt));
        }
        if(rc != SQLITE_DONE && rc != SQLITE_ROW) {
            spdlog::error("[Database]: Failed to fetch all rows: {}",
                          sqlite3_errmsg(db));
            throw std::runtime_error("");
        } else {
            spdlog::trace("[Database]: Successfully fetched all articles");
        }
        sqlite3_reset(get_all_stmt);
    } else {
        spdlog::debug("[Database]: Getting all articles within last {} days", days);
        rc = sqlite3_bind_int(get_recent_stmt, 1, days);
        if(rc != SQLITE_OK) {
            spdlog::error("[Database]: Failed to bind parameters: {}", sqlite3_errmsg(db));
            return articles;
        }

        while((rc = sqlite3_step(get_recent_stmt)) == SQLITE_ROW) {
            articles.push_back(RowToArticle(get_recent_stmt));
        }
        if(rc != SQLITE_DONE && rc != SQLITE_ROW) {
            spdlog::error("[Database]: Failed to fetch all rows: {}",
                          sqlite3_errmsg(db));
            throw std::runtime_error("");
        } else {
            spdlog::trace("[Database]: Successfully fetched all articles");
        }
        sqlite3_reset(get_recent_stmt);
    }
    return articles;
}

std::vector<Arxiv::Article> DatabaseManager::ListBookmarked() {
    std::vector<Article> articles;
    spdlog::debug("[Database]: Collecting all bookmarked articles");

    while(sqlite3_step(get_bookmarks_stmt) == SQLITE_ROW) {
        articles.push_back(RowToArticle(get_bookmarks_stmt));
    }
    spdlog::trace("[Database]: Found {} bookmarks", articles.size());

    sqlite3_reset(get_bookmarks_stmt);
    return articles;
}

void DatabaseManager::Initialize() {
    const char *init_sql = R"(
        CREATE TABLE IF NOT EXISTS articles (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            link TEXT UNIQUE NOT NULL,
            abstract TEXT,
            category TEXT,
            authors TEXT,
            published_date INTEGER,
            bookmarked INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        );
        CREATE INDEX IF NOT EXISTS idx_link ON articles(link);
        CREATE INDEX IF NOT EXISTS idx_date ON articles(published_date);
        CREATE INDEX IF NOT EXISTS idx_bookmarked ON articles(bookmarked);
    )";

    char *err_msg = nullptr;
    int rc = sqlite3_exec(db, init_sql, nullptr, nullptr, &err_msg);

    if(rc != SQLITE_OK) {
        std::string error = err_msg;
        sqlite3_free(err_msg);
        throw std::runtime_error("[Database] SQL Error: " + error);
    }
}

void DatabaseManager::PrepareStatements() {
    // Insert statement
    const char *insert_sql = R"(
        INSERT OR IGNORE INTO articles
        (title, link, abstract, category, authors, published_date)
        VALUES (?, ?, ?, ?, ?, ?)
    )";
    int rc = sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr);
    if(rc != SQLITE_OK) {
        spdlog::error("[Database]: Failed to prepare insert statement: {}", sqlite3_errmsg(db));
        throw std::runtime_error("[Database] Failed to prepare insert statement.");
    }

    // Bookmark statement
    const char *bookmark_sql = R"(
        UPDATE articles SET bookmarked = ? WHERE link = ?
    )";
    rc = sqlite3_prepare_v2(db, bookmark_sql, -1, &bookmark_stmt, nullptr);
    if(rc != SQLITE_OK) {
        spdlog::error("[Database]: Failed to prepare insert statement: {}", sqlite3_errmsg(db));
        throw std::runtime_error("[Database] Failed to prepare insert statement.");
    }

    // All articles statement
    const char *all_sql = R"(
        SELECT * FROM articles
        ORDER BY published_date DESC
    )";
    rc = sqlite3_prepare_v2(db, all_sql, -1, &get_all_stmt, nullptr);
    if(rc != SQLITE_OK) {
        spdlog::error("[Database]: Failed to prepare insert statement: {}", sqlite3_errmsg(db));
        throw std::runtime_error("[Database] Failed to prepare insert statement.");
    }

    // Recent articles statement
    const char *recent_sql = R"(
        SELECT * FROM articles
        WHERE published_date >= strftime('%s', 'now', '-? days')
        ORDER BY published_date DESC
    )";
    rc = sqlite3_prepare_v2(db, recent_sql, -1, &get_recent_stmt, nullptr);
    if(rc != SQLITE_OK) {
        spdlog::error("[Database]: Failed to prepare insert statement: {}", sqlite3_errmsg(db));
        throw std::runtime_error("[Database] Failed to prepare insert statement.");
    }

    // Bookmarked articles statement
    const char *bookmarks_sql = R"(
        SELECT * FROM articles WHERE bookmarked = 1
        ORDER BY published_date DESC
    )";
    rc = sqlite3_prepare_v2(db, bookmarks_sql, -1, &get_bookmarks_stmt, nullptr);
    if(rc != SQLITE_OK) {
        spdlog::error("[Database]: Failed to prepare insert statement: {}", sqlite3_errmsg(db));
        throw std::runtime_error("[Database] Failed to prepare insert statement.");
    }
}

const char* DatabaseManager::ExtractColumn(sqlite3_stmt *stmt, int index) {
    const unsigned char *text = sqlite3_column_text(stmt, index);
    return text ? reinterpret_cast<const char*>(text) : "";
}

Arxiv::Article DatabaseManager::RowToArticle(sqlite3_stmt *stmt) {
    Article article;
    article.title = ExtractColumn(stmt, 1);
    article.link = ExtractColumn(stmt,2);
    article.abstract = ExtractColumn(stmt,3);
    article.category = ExtractColumn(stmt,4);
    article.authors = ExtractColumn(stmt,5);

    // Convert Unix timestamp back to time_point
    int64_t timestamp = sqlite3_column_int64(stmt, 6);
    article.date = std::chrono::system_clock::from_time_t(timestamp);

    article.bookmarked = sqlite3_column_int(stmt, 7) != 0;

    return article;
}

int DatabaseManager::TraceCallback(unsigned type, void *, void *p, void *) {
    if(type == SQLITE_TRACE_STMT) {
        const char *sql = static_cast<const char*>(p);
        if(sql) {
            spdlog::trace("[SQL]: {}", sql);
        }
    }
    return 0;
}
