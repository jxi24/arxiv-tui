#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Article.hh"
#include "spdlog/spdlog.h"
#include <chrono>
#include <sqlite3.h>
#include <stdexcept>
#include <ctime>

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

void DatabaseManager::AddArticle(const Article &article) {
    spdlog::debug("[Database]: Adding article: {}", article.link);
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        article.date.time_since_epoch()).count();

    const char* sql =
        "INSERT OR REPLACE INTO articles (link, title, authors, abstract, date, bookmarked) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, article.link.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, article.title.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, article.authors.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, article.abstract.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(timestamp));
    sqlite3_bind_int(stmt,  6, article.bookmarked ? 1 : 0);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("[Database]: AddArticle failed: ") + sqlite3_errmsg(db));
    }
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
    const char* sql = "SELECT link, title, authors, abstract, date, bookmarked FROM articles WHERE bookmarked = 1";
    if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            articles.push_back(RowToArticle(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return articles;
}

void DatabaseManager::ToggleBookmark(const std::string &link, bool bookmarked) {
    spdlog::debug("[Database]: Toggling bookmark for {}", link);
    const char* sql = "UPDATE articles SET bookmarked = ? WHERE link = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_int(stmt,  1, bookmarked ? 1 : 0);
    sqlite3_bind_text(stmt, 2, link.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("[Database]: ToggleBookmark failed: ") + sqlite3_errmsg(db));
    }
}

void DatabaseManager::AddProject(const std::string &project_name) {
    const char* sql = "INSERT OR REPLACE INTO projects (name) VALUES (?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, project_name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("[Database]: AddProject failed: ") + sqlite3_errmsg(db));
    }
}

void DatabaseManager::RemoveProject(const std::string &project_name) {
    ExecuteSQL("BEGIN TRANSACTION");
    auto bind_and_step = [&](const char* sql) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            ExecuteSQL("ROLLBACK");
            throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
        }
        sqlite3_bind_text(stmt, 1, project_name.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            ExecuteSQL("ROLLBACK");
            throw std::runtime_error(std::string("[Database]: RemoveProject failed: ") + sqlite3_errmsg(db));
        }
    };
    bind_and_step("DELETE FROM projects WHERE name = ?");
    bind_and_step("DELETE FROM project_articles WHERE project_name = ?");
    bind_and_step("DELETE FROM project_notes WHERE project_name = ?");
    bind_and_step("UPDATE projects SET parent = '' WHERE parent = ?");
    ExecuteSQL("COMMIT");
}

std::vector<std::string> DatabaseManager::GetProjects() {
    std::vector<std::string> projects;
    sqlite3_stmt *stmt;
    const char* sql = "SELECT name FROM projects";
    if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            projects.push_back(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }
    return projects;
}

void DatabaseManager::LinkArticleToProject(const std::string &article_link, const std::string &project_name) {
    spdlog::debug("[Database]: Linking article {} to project {}", article_link, project_name);
    const char* sql = "INSERT OR IGNORE INTO project_articles (project_name, article_link) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, project_name.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, article_link.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("[Database]: LinkArticleToProject failed: ") + sqlite3_errmsg(db));
    }
}

void DatabaseManager::UnlinkArticleFromProject(const std::string &article_link, const std::string &project_name) {
    spdlog::debug("[Database]: Unlinking article {} from project {}", article_link, project_name);
    const char* sql = "DELETE FROM project_articles WHERE project_name = ? AND article_link = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, project_name.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, article_link.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("[Database]: UnlinkArticleFromProject failed: ") + sqlite3_errmsg(db));
    }
}

std::vector<Arxiv::Article> DatabaseManager::GetArticlesForProject(const std::string &project_name) {
    std::vector<Article> articles;
    sqlite3_stmt *stmt;
    const char* sql = R"(SELECT a.link, a.title, a.authors, a.abstract, a.date, a.bookmarked
                         FROM articles a
                         JOIN project_articles pa ON a.link = pa.article_link
                         WHERE pa.project_name = ?)";
    spdlog::debug("[Database]: Collecting articles for project {}", project_name);
    if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, project_name.c_str(), -1, SQLITE_TRANSIENT);
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
    article.link     = ExtractColumn(stmt, 0);
    article.title    = ExtractColumn(stmt, 1);
    article.authors  = ExtractColumn(stmt, 2);
    article.abstract = ExtractColumn(stmt, 3);

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

std::vector<std::string> DatabaseManager::GetProjectsForArticle(const std::string &article_link) {
    std::vector<std::string> projects;
    sqlite3_stmt *stmt;
    const char* sql = "SELECT project_name FROM project_articles WHERE article_link = ?";
    spdlog::debug("[Database]: Getting projects for article {}", article_link);
    if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, article_link.c_str(), -1, SQLITE_TRANSIENT);
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            projects.push_back(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }
    return projects;
}

std::vector<Arxiv::Article> DatabaseManager::GetArticlesForDateRange(const std::string &start_date, const std::string &end_date) {
    std::vector<Article> articles;
    sqlite3_stmt *stmt;

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
    time_t end_time   = mktime(&end_tm) + 24 * 60 * 60;

    const char* sql = R"(SELECT link, title, authors, abstract, date, bookmarked
                          FROM articles
                          WHERE date >= ? AND date < ?
                          ORDER BY date DESC)";

    spdlog::debug("[Database]: Fetching articles between {} and {}", start_date, end_date);

    if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(start_time));
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(end_time));
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            articles.push_back(RowToArticle(stmt));
        }
        sqlite3_finalize(stmt);
    }

    spdlog::debug("[Database]: Found {} articles in date range", articles.size());
    return articles;
}

void DatabaseManager::SetRating(const std::string &link, int rating) {
    spdlog::debug("[Database]: Setting rating {} for {}", rating, link);
    const char* sql = "INSERT OR REPLACE INTO article_ratings (article_link, rating) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, link.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,  2, rating);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("[Database]: SetRating failed: ") + sqlite3_errmsg(db));
    }
}

int DatabaseManager::GetRating(const std::string &link) {
    sqlite3_stmt *stmt;
    const char* sql = "SELECT rating FROM article_ratings WHERE article_link = ?";
    int rating = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, link.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            rating = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return rating;
}

DatabaseManager::RatedArticleList DatabaseManager::GetRatedArticles() {
    RatedArticleList result;
    sqlite3_stmt *stmt;
    const char* sql = R"(SELECT a.link, a.title, a.authors, a.abstract, a.date, a.bookmarked,
                                r.rating
                         FROM articles a
                         JOIN article_ratings r ON a.link = r.article_link)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Article article = RowToArticle(stmt);
            int rating = sqlite3_column_int(stmt, 6);
            result.emplace_back(std::move(article), rating);
        }
        sqlite3_finalize(stmt);
    }
    spdlog::debug("[Database]: Found {} rated articles", result.size());
    return result;
}

std::string DatabaseManager::GetProjectParent(const std::string &project_name) {
    sqlite3_stmt *stmt;
    const char* sql = "SELECT COALESCE(parent, '') FROM projects WHERE name = ?";
    std::string parent;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, project_name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            parent = ExtractColumn(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return parent;
}

void DatabaseManager::SetProjectParent(const std::string &project_name, const std::string &parent) {
    const char* sql = "UPDATE projects SET parent = ? WHERE name = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, parent.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, project_name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("[Database]: SetProjectParent failed: ") + sqlite3_errmsg(db));
    }
}

void DatabaseManager::SetProjectNote(const std::string &project_name, const std::string &article_link,
                                     const std::string &note) {
    spdlog::debug("[Database]: Setting note for article {} in project {}", article_link, project_name);
    const char* sql =
        "INSERT OR REPLACE INTO project_notes (project_name, article_link, note) VALUES (?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, project_name.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, article_link.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, note.c_str(),          -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("[Database]: SetProjectNote failed: ") + sqlite3_errmsg(db));
    }
}

std::string DatabaseManager::GetProjectNote(const std::string &project_name, const std::string &article_link) {
    sqlite3_stmt *stmt;
    const char* sql = "SELECT note FROM project_notes WHERE project_name = ? AND article_link = ?";
    std::string note;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, project_name.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, article_link.c_str(),  -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            note = ExtractColumn(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return note;
}

std::vector<Arxiv::Article> DatabaseManager::SearchArticles(const std::string &query, bool search_title,
                                                            bool search_authors, bool search_abstract) {
    std::vector<Article> articles;

    std::vector<std::string> conditions;
    if (search_title)    conditions.push_back("title LIKE ? ESCAPE '\\'");
    if (search_authors)  conditions.push_back("authors LIKE ? ESCAPE '\\'");
    if (search_abstract) conditions.push_back("abstract LIKE ? ESCAPE '\\'");

    if (conditions.empty()) {
        spdlog::warn("[Database]: No search fields selected");
        return articles;
    }

    std::string where_clause = "WHERE " + conditions[0];
    for (size_t i = 1; i < conditions.size(); ++i)
        where_clause += " OR " + conditions[i];

    std::string sql = "SELECT link, title, authors, abstract, date, bookmarked FROM articles " +
                      where_clause + " ORDER BY date DESC";

    spdlog::debug("[Database]: Searching for '{}' in title: {}, authors: {}, abstract: {}",
                  query, search_title, search_authors, search_abstract);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        // Escape LIKE metacharacters in the query itself
        std::string escaped_query;
        escaped_query.reserve(query.size() + 4);
        for (char c : query) {
            if (c == '%' || c == '_' || c == '\\') escaped_query += '\\';
            escaped_query += c;
        }
        std::string pattern = "%" + escaped_query + "%";

        int param = 1;
        if (search_title)    sqlite3_bind_text(stmt, param++, pattern.c_str(), -1, SQLITE_TRANSIENT);
        if (search_authors)  sqlite3_bind_text(stmt, param++, pattern.c_str(), -1, SQLITE_TRANSIENT);
        if (search_abstract) sqlite3_bind_text(stmt, param++, pattern.c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            articles.push_back(RowToArticle(stmt));
        sqlite3_finalize(stmt);
    }

    spdlog::debug("[Database]: Found {} articles matching search criteria", articles.size());
    return articles;
}

void DatabaseManager::SetRelevanceScore(const std::string &link, float score) {
    const char* sql = "UPDATE articles SET relevance_score = ? WHERE link = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_double(stmt, 1, static_cast<double>(score));
    sqlite3_bind_text(stmt,  2, link.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("[Database]: SetRelevanceScore failed: ") + sqlite3_errmsg(db));
    }
}

float DatabaseManager::GetRelevanceScore(const std::string &link) {
    sqlite3_stmt *stmt;
    const char* sql = "SELECT relevance_score FROM articles WHERE link = ?";
    float score = 0.0f;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, link.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            score = static_cast<float>(sqlite3_column_double(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return score;
}

void DatabaseManager::FollowAuthor(const std::string &author_name) {
    const char* sql = "INSERT OR IGNORE INTO followed_authors (author_name) VALUES (?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, author_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DatabaseManager::UnfollowAuthor(const std::string &author_name) {
    const char* sql = "DELETE FROM followed_authors WHERE author_name = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, author_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

bool DatabaseManager::IsFollowingAuthor(const std::string &author_name) {
    sqlite3_stmt *stmt;
    const char* sql = "SELECT 1 FROM followed_authors WHERE author_name = ?";
    bool found = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, author_name.c_str(), -1, SQLITE_TRANSIENT);
        found = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
    }
    return found;
}

std::vector<std::string> DatabaseManager::GetFollowedAuthors() {
    std::vector<std::string> authors;
    sqlite3_stmt *stmt;
    const char* sql = "SELECT author_name FROM followed_authors";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW)
            authors.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        sqlite3_finalize(stmt);
    }
    return authors;
}

void DatabaseManager::SetMetadata(const std::string &key, const std::string &value) {
    const char* sql = "INSERT OR REPLACE INTO metadata (key, value) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("[Database]: prepare failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_bind_text(stmt, 1, key.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string DatabaseManager::GetMetadata(const std::string &key) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT value FROM metadata WHERE key = ?";
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (val) result = val;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

std::vector<Arxiv::Article> DatabaseManager::GetArticlesSince(const std::string &utc_date) {
    // Convert "YYYY-MM-DD" to UTC midnight Unix timestamp.
    std::tm tm{};
    tm.tm_year = std::stoi(utc_date.substr(0, 4)) - 1900;
    tm.tm_mon  = std::stoi(utc_date.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(utc_date.substr(8, 2));
    std::time_t since_ts = timegm(&tm);

    std::vector<Arxiv::Article> articles;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT link, title, authors, abstract, date, bookmarked "
                      "FROM articles WHERE date >= ? ORDER BY date DESC";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(since_ts));
        while (sqlite3_step(stmt) == SQLITE_ROW)
            articles.push_back(RowToArticle(stmt));
        sqlite3_finalize(stmt);
    }
    return articles;
}
