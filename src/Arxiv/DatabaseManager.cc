// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/DatabaseManager.hh"

#include "Arxiv/Article.hh"
#include "Arxiv/Fetcher.hh"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <sqlite3.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>

#include "spdlog/spdlog.h"

using Arxiv::DatabaseManager;

// ---------------------------------------------------------------------------
// SQL helpers
// ---------------------------------------------------------------------------
namespace {

// The article column list appears in nearly every SELECT — keep it in one
// place so adding a column does not require touching seven query strings.
constexpr const char* ARTICLE_COLUMNS =
    "link, title, authors, abstract, date, bookmarked, category, is_replacement, read_at";
constexpr const char* ARTICLE_COLUMNS_A =
    "a.link, a.title, a.authors, a.abstract, a.date, a.bookmarked, a.category, a.is_replacement,"
    " a.read_at";

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
        // Explicit length: with -1 SQLite stops at an embedded NUL byte.
        sqlite3_bind_text(m_stmt, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
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
        int rc;
        while ((rc = sqlite3_step(m_stmt)) == SQLITE_ROW)
            on_row(m_stmt);
        // Anything but DONE (BUSY, a malformed FTS query, I/O) used to end
        // the loop silently with a partial result.
        if (rc != SQLITE_DONE)
            throw std::runtime_error(std::string("[Database]: ") + std::string(m_op) +
                                     " failed: " + sqlite3_errmsg(m_db));
    }

    sqlite3_stmt* raw() { return m_stmt; }
    /// Ready the statement to run again (bindings are replaced by the next bind).
    void reset() { sqlite3_reset(m_stmt); }

  private:
    sqlite3* m_db;
    sqlite3_stmt* m_stmt = nullptr;
    std::string_view m_op;
};

} // namespace

DatabaseManager::DatabaseManager(const std::string& path) {
    spdlog::info("[Database]: Opening database at {}", path);
    // A new database holds notes and ratings: create it mode 0600 (SQLite
    // gives its -wal and -shm files the same mode).
    if (!path.empty() && path != ":memory:" && path.rfind("file:", 0) != 0 &&
        !std::filesystem::exists(path)) {
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0)
            ::close(fd);
    }
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::string error = db ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close(db); // a handle is returned even on failure
        throw std::runtime_error("[Database]: Can't open database: " + error);
    }
    try {
        Initialise();
    } catch (...) {
        sqlite3_close(db); // the destructor does not run for a throwing ctor
        throw;
    }
    spdlog::info("[Database]: Initialized");
}

std::unique_ptr<DatabaseManager> DatabaseManager::InMemoryCopyOf(const std::string& path) {
    sqlite3* source = nullptr;
    if (sqlite3_open_v2(path.c_str(), &source, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::string error = source ? sqlite3_errmsg(source) : "out of memory";
        sqlite3_close(source);
        throw std::runtime_error("[Database]: Can't open " + path + ": " + error);
    }
    auto copy = std::make_unique<DatabaseManager>(":memory:");
    std::lock_guard<std::recursive_mutex> lock(copy->m_mutex);
    sqlite3_backup* backup = sqlite3_backup_init(copy->db, "main", source, "main");
    int rc = backup ? sqlite3_backup_step(backup, -1) : SQLITE_ERROR;
    sqlite3_backup_finish(backup);
    std::string error = sqlite3_errmsg(copy->db);
    sqlite3_close(source);
    if (rc != SQLITE_DONE)
        throw std::runtime_error("[Database]: Can't copy " + path + ": " + error);
    copy->Initialise(); // migrate a copy of an older file like the file itself
    return copy;
}

namespace {

// Link tables. Every row belongs to an article (and a project or tag): the
// keys cascade, so deleting or renaming the parent takes its rows with it.
// "%s" is the table name (the version-1 migration builds them under a
// temporary name first).
constexpr const char* kLinkTables[][2] = {
    {"project_articles",
     R"(CREATE TABLE IF NOT EXISTS %s (
            project_name TEXT REFERENCES projects(name) ON DELETE CASCADE ON UPDATE CASCADE,
            article_link TEXT REFERENCES articles(link) ON DELETE CASCADE ON UPDATE CASCADE,
            PRIMARY KEY (project_name, article_link)))"},
    {"article_ratings",
     R"(CREATE TABLE IF NOT EXISTS %s (
            article_link TEXT PRIMARY KEY
                REFERENCES articles(link) ON DELETE CASCADE ON UPDATE CASCADE,
            rating INTEGER NOT NULL))"},
    {"project_notes",
     R"(CREATE TABLE IF NOT EXISTS %s (
            project_name TEXT REFERENCES projects(name) ON DELETE CASCADE ON UPDATE CASCADE,
            article_link TEXT REFERENCES articles(link) ON DELETE CASCADE ON UPDATE CASCADE,
            note TEXT,
            PRIMARY KEY (project_name, article_link)))"},
    {"article_tags",
     R"(CREATE TABLE IF NOT EXISTS %s (
            article_link TEXT REFERENCES articles(link) ON DELETE CASCADE ON UPDATE CASCADE,
            tag_name TEXT REFERENCES tags(name) ON DELETE CASCADE ON UPDATE CASCADE,
            PRIMARY KEY (article_link, tag_name)))"},
};

std::string link_table_sql(const char* create, const std::string& name) {
    std::string sql = create;
    sql.replace(sql.find("%s"), 2, name);
    return sql;
}

// Rows whose parents all exist (the copy step of the version-1 rebuild).
const char* link_table_filter(const std::string& table) {
    if (table == "project_articles" || table == "project_notes")
        return "project_name IN (SELECT name FROM projects) AND "
               "article_link IN (SELECT link FROM articles)";
    if (table == "article_tags")
        return "article_link IN (SELECT link FROM articles) AND "
               "tag_name IN (SELECT name FROM tags)";
    return "article_link IN (SELECT link FROM articles)";
}

} // namespace

void DatabaseManager::Initialise() {
    sqlite3_trace_v2(db, SQLITE_TRACE_STMT, DatabaseManager::TraceCallback, nullptr);
    // The TUI and a headless --fetch (cron) may use the file at the same
    // time: WAL lets readers proceed during a write, and the busy timeout
    // makes a writer wait for the other's lock instead of failing at once.
    sqlite3_busy_timeout(db, 5000);
    CheckSchemaVersion();
    ExecuteSQL("PRAGMA journal_mode=WAL");

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

    // Link tables (project membership, ratings, notes; tags below).
    for (const auto& [name, create] : kLinkTables)
        if (std::string(name) != "article_tags")
            ExecuteSQL(link_table_sql(create, name));

    AddColumnIfMissing("projects", "parent", "TEXT DEFAULT ''");

    AddColumnIfMissing("articles", "relevance_score", "REAL DEFAULT 0.0");

    AddColumnIfMissing("articles", "category", "TEXT DEFAULT ''");

    // Used by the New Articles view to hide updates of older submissions.
    AddColumnIfMissing("articles", "is_replacement", "INTEGER DEFAULT 0");

    // Create followed_authors table
    ExecuteSQL(R"(CREATE TABLE IF NOT EXISTS followed_authors (
               author_name TEXT PRIMARY KEY))");

    // Create metadata key-value table
    ExecuteSQL(R"(CREATE TABLE IF NOT EXISTS metadata (
               key   TEXT PRIMARY KEY,
               value TEXT NOT NULL DEFAULT ''))");

    MigrateNormalizeLinks();
    MigrateAddReadAt();
    MigrateAddAddedAt();
    MigrateAddUpdatedAt();
    MigrateAddProjectBibPath();
    CreateTagTables();
    MigrateAddFTS5();
    MigrateFTSUpdateTrigger();
    MigrateSchema();
}

void DatabaseManager::CheckSchemaVersion() {
    Stmt stmt(db, "PRAGMA user_version", "CheckSchemaVersion");
    const int version = stmt.step() == SQLITE_ROW ? sqlite3_column_int(stmt.raw(), 0) : 0;
    if (version > kSchemaVersion)
        throw std::runtime_error(
            "[Database]: The database was written by a newer arxiv-tui (schema version " +
            std::to_string(version) + ", this build knows " + std::to_string(kSchemaVersion) +
            "); refusing to open it");
}

void DatabaseManager::MigrateSchema() {
    int version = 0;
    {
        Stmt stmt(db, "PRAGMA user_version", "MigrateSchema/version");
        if (stmt.step() == SQLITE_ROW)
            version = sqlite3_column_int(stmt.raw(), 0);
    }
    // Rebuilding tables needs the keys off (SQLite's documented procedure);
    // they are switched on for good afterwards.
    if (version < 1)
        MigrateToCascadingLinks();
    if (version < 2) {
        // Papers the user deleted: fetches skip them; AddArticle (undo,
        // import) brings one back with its original added_at.
        ExecuteSQL("CREATE TABLE IF NOT EXISTS deleted_articles ("
                   "link TEXT PRIMARY KEY, deleted_at INTEGER, added_at INTEGER)");
    }
    ExecuteSQL("PRAGMA user_version = " + std::to_string(kSchemaVersion));
    ExecuteSQL("PRAGMA foreign_keys = ON");
}

void DatabaseManager::MigrateToCascadingLinks() {
    ExecuteSQL("BEGIN TRANSACTION");
    try {
        for (const auto& [name, create] : kLinkTables) {
            const std::string table = name;
            const std::string staged = table + "_v1";
            ExecuteSQL("DROP TABLE IF EXISTS " + staged);
            ExecuteSQL(link_table_sql(create, staged));
            // Orphans (parents long gone; keys were never enforced) cannot
            // satisfy the new keys and were invisible anyway: leave them out.
            ExecuteSQL("INSERT INTO " + staged + " SELECT * FROM " + table + " WHERE " +
                       link_table_filter(table));
            ExecuteSQL("DROP TABLE " + table);
            ExecuteSQL("ALTER TABLE " + staged + " RENAME TO " + table);
        }
        ExecuteSQL("COMMIT");
    } catch (...) {
        ExecuteSQL("ROLLBACK");
        throw;
    }
    spdlog::info("[Database]: Schema version 1: link tables now cascade");
}

void DatabaseManager::MigrateNormalizeLinks() {
    // One-time migration: normalize all article links to canonical form
    // (https scheme, no version suffix). Tracked by a metadata key so it
    // only runs once even if the DB is opened many times.
    {
        Stmt check(db,
                   "SELECT value FROM metadata WHERE key = 'migration_normalize_links'",
                   "MigrateNormalizeLinks/check");
        if (check.step() == SQLITE_ROW)
            return;
    }

    // Collect every link that differs from its canonical form.
    std::vector<std::pair<std::string, std::string>> to_rename;
    {
        Stmt sel(db, "SELECT link FROM articles", "MigrateNormalizeLinks/select");
        sel.for_each([&](sqlite3_stmt* s) {
            const auto* text = sqlite3_column_text(s, 0);
            if (!text)
                return;
            std::string link = reinterpret_cast<const char*>(text);
            const std::string canonical = Fetcher::NormalizeLink(link);

            if (canonical != link)
                to_rename.emplace_back(link, canonical);
        });
    }

    for (auto& [old_link, canonical] : to_rename) {
        bool canonical_exists = false;
        {
            Stmt ex(db, "SELECT 1 FROM articles WHERE link = ?", "MigrateNormalizeLinks/exists");
            ex.bind(1, canonical);
            canonical_exists = (ex.step() == SQLITE_ROW);
        }

        if (canonical_exists) {
            // Merge: propagate bookmark if the non-canonical copy has it set.
            {
                Stmt bm(db,
                        "SELECT bookmarked FROM articles WHERE link = ?",
                        "MigrateNormalizeLinks/bm");
                bm.bind(1, old_link);
                if (bm.step() == SQLITE_ROW && sqlite3_column_int(bm.raw(), 0)) {
                    Stmt set(db,
                             "UPDATE articles SET bookmarked = 1 WHERE link = ?",
                             "MigrateNormalizeLinks/setbm");
                    set.bind(1, canonical).step_done();
                }
            }
            // Merge rating (canonical wins on conflict).
            Stmt mr(db,
                    "INSERT OR IGNORE INTO article_ratings (article_link, rating) "
                    "SELECT ?, rating FROM article_ratings WHERE article_link = ?",
                    "MigrateNormalizeLinks/mergerating");
            mr.bind(1, canonical).bind(2, old_link).step_done();

            // Merge project memberships.
            Stmt mpa(db,
                     "INSERT OR IGNORE INTO project_articles (project_name, article_link) "
                     "SELECT project_name, ? FROM project_articles WHERE article_link = ?",
                     "MigrateNormalizeLinks/mergepа");
            mpa.bind(1, canonical).bind(2, old_link).step_done();

            // Merge project notes (canonical wins on conflict).
            Stmt mn(db,
                    "INSERT OR IGNORE INTO project_notes (project_name, article_link, note) "
                    "SELECT project_name, ?, note FROM project_notes WHERE article_link = ?",
                    "MigrateNormalizeLinks/mergenotes");
            mn.bind(1, canonical).bind(2, old_link).step_done();

            // Remove all FK rows for the old link, then the article itself.
            Stmt dpa(db,
                     "DELETE FROM project_articles WHERE article_link = ?",
                     "MigrateNormalizeLinks/dpa");
            dpa.bind(1, old_link).step_done();
            Stmt dr(db,
                    "DELETE FROM article_ratings WHERE article_link = ?",
                    "MigrateNormalizeLinks/dr");
            dr.bind(1, old_link).step_done();
            Stmt dn(
                db, "DELETE FROM project_notes WHERE article_link = ?", "MigrateNormalizeLinks/dn");
            dn.bind(1, old_link).step_done();
            Stmt da(db, "DELETE FROM articles WHERE link = ?", "MigrateNormalizeLinks/da");
            da.bind(1, old_link).step_done();
        } else {
            // No collision — copy article under canonical link, re-point FK rows, delete old.
            Stmt ins(db,
                     "INSERT INTO articles "
                     "(link,title,authors,abstract,date,bookmarked,relevance_score,category,"
                     "is_replacement) "
                     "SELECT ?,title,authors,abstract,date,bookmarked,relevance_score,category,"
                     "is_replacement FROM articles WHERE link = ?",
                     "MigrateNormalizeLinks/insert");
            ins.bind(1, canonical).bind(2, old_link).step_done();

            Stmt upa(db,
                     "UPDATE project_articles SET article_link = ? WHERE article_link = ?",
                     "MigrateNormalizeLinks/upa");
            upa.bind(1, canonical).bind(2, old_link).step_done();
            Stmt ur(db,
                    "UPDATE article_ratings SET article_link = ? WHERE article_link = ?",
                    "MigrateNormalizeLinks/ur");
            ur.bind(1, canonical).bind(2, old_link).step_done();
            Stmt un(db,
                    "UPDATE project_notes SET article_link = ? WHERE article_link = ?",
                    "MigrateNormalizeLinks/un");
            un.bind(1, canonical).bind(2, old_link).step_done();

            Stmt del(db, "DELETE FROM articles WHERE link = ?", "MigrateNormalizeLinks/del");
            del.bind(1, old_link).step_done();
        }

        spdlog::info("[Database]: Normalized link: {} -> {}", old_link, canonical);
    }

    Stmt mark(db,
              "INSERT OR REPLACE INTO metadata (key, value) "
              "VALUES ('migration_normalize_links', 'done')",
              "MigrateNormalizeLinks/mark");
    mark.step_done();

    if (!to_rename.empty())
        spdlog::info("[Database]: Link normalization migrated {} article(s)", to_rename.size());
}

DatabaseManager::~DatabaseManager() {
    spdlog::info("[Database]: Closing database");
    sqlite3_close(db);
}

void DatabaseManager::AddColumnIfMissing(const std::string& table,
                                         const std::string& column,
                                         const std::string& declaration) {
    // Checked via PRAGMA table_info instead of catching the ALTER's error,
    // which also swallowed real failures (disk full, locked database).
    bool present = false;
    {
        Stmt info(db, ("PRAGMA table_info(" + table + ")").c_str(), "AddColumnIfMissing");
        info.for_each([&](sqlite3_stmt* st) {
            if (ExtractColumn(st, 1) == column)
                present = true;
        });
    }
    if (!present)
        ExecuteSQL("ALTER TABLE " + table + " ADD COLUMN " + column + " " + declaration);
}

void DatabaseManager::MigrateFTSUpdateTrigger() {
    // The original trigger fired on every UPDATE of articles, so bookmarking,
    // read marks and relevance scores each rewrote the article's FTS row.
    {
        Stmt check(db,
                   "SELECT sql FROM sqlite_master WHERE type = 'trigger' AND name = 'articles_au'",
                   "MigrateFTSUpdateTrigger/check");
        if (check.step() == SQLITE_ROW &&
            ExtractColumn(check.raw(), 0).find("UPDATE OF title, authors, abstract") !=
                std::string::npos)
            return;
    }
    ExecuteSQL("BEGIN");
    try {
        ExecuteSQL("DROP TRIGGER IF EXISTS articles_au");
        ExecuteSQL(R"(CREATE TRIGGER articles_au
            AFTER UPDATE OF title, authors, abstract ON articles BEGIN
                INSERT INTO articles_fts(articles_fts, rowid, title, authors, abstract)
                VALUES ('delete', old.rowid, old.title, old.authors, old.abstract);
                INSERT INTO articles_fts(rowid, title, authors, abstract)
                VALUES (new.rowid, new.title, new.authors, new.abstract);
            END)");
        ExecuteSQL("COMMIT");
    } catch (...) {
        ExecuteSQL("ROLLBACK");
        throw;
    }
}

void DatabaseManager::ExecuteSQL(const std::string& sql) {
    char* errmsg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string error_msg = errmsg ? errmsg : sqlite3_errmsg(db);
        sqlite3_free(errmsg);
        throw std::runtime_error(error_msg);
    }
}

void DatabaseManager::AddArticles(const std::vector<Article>& articles) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (articles.empty())
        return;
    ExecuteSQL("BEGIN TRANSACTION");
    try {
        // A paper the user deleted stays deleted, however often the feed
        // lists it again.
        Stmt deleted(db, "SELECT 1 FROM deleted_articles WHERE link = ?", "AddArticles/deleted");
        for (const auto& a : articles) {
            deleted.reset();
            if (deleted.bind(1, a.link).step() == SQLITE_ROW)
                continue;
            AddArticle(a);
        }
        ExecuteSQL("COMMIT");
    } catch (...) {
        ExecuteSQL("ROLLBACK");
        throw;
    }
}

void DatabaseManager::AddArticle(const Article& article) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug("[Database]: Adding article: {}", article.link);
    auto timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(article.date.time_since_epoch()).count();

    // Upsert rather than INSERT OR REPLACE. arXiv re-lists a paper every time a
    // new version is posted, and the feed then carries the *announcement* date
    // of that version. Only the feed-owned metadata may be refreshed: `date`
    // stays at the first release so a v2 never resurfaces a months-old paper at
    // the top of the list, and the user-owned columns (bookmarked, read_at,
    // relevance_score) plus the insertion time survive untouched.
    // `updated_at` records the last time the feed carried this paper as a
    // replacement — the signal behind the Updated view. It is driven by the
    // replacement flag, not by the presence of a conflict: FetchSince's
    // backfill window re-fetches several days of unchanged papers on every run,
    // so a conflicting row proves nothing about a new version. A paper first
    // seen as a replacement is stamped on insert; one already stored is stamped
    // on update; a plain re-fetch keeps whatever stamp was there.
    Stmt stmt(db,
              "INSERT INTO articles "
              "(link, title, authors, abstract, date, bookmarked, category, is_replacement, "
              " added_at, updated_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, strftime('%s', 'now'), "
              "        CASE WHEN ? THEN strftime('%s', 'now') ELSE NULL END) "
              "ON CONFLICT(link) DO UPDATE SET "
              "  title = excluded.title, "
              "  authors = excluded.authors, "
              "  abstract = excluded.abstract, "
              "  category = excluded.category, "
              "  is_replacement = excluded.is_replacement, "
              "  updated_at = CASE WHEN excluded.is_replacement THEN strftime('%s', 'now') "
              "                    ELSE articles.updated_at END",
              "AddArticle");
    stmt.bind(1, article.link)
        .bind(2, article.title)
        .bind(3, article.authors)
        .bind(4, article.abstract)
        .bind(5, static_cast<sqlite3_int64>(timestamp))
        .bind(6, article.bookmarked ? 1 : 0)
        .bind(7, article.category)
        .bind(8, article.is_replacement ? 1 : 0)
        .bind(9, article.is_replacement ? 1 : 0);
    stmt.step_done();

    // Adding a paper the user deleted (undo, an import) brings it back as it
    // was: with the time it was first stored, so it is not "new" again.
    Stmt restore(db,
                 "UPDATE articles SET added_at = "
                 "  COALESCE((SELECT added_at FROM deleted_articles WHERE link = ?1), added_at) "
                 "WHERE link = ?1 AND EXISTS (SELECT 1 FROM deleted_articles WHERE link = ?1)",
                 "AddArticle/restore");
    restore.bind(1, article.link).step_done();
    Stmt undelete(db, "DELETE FROM deleted_articles WHERE link = ?", "AddArticle/undelete");
    undelete.bind(1, article.link).step_done();
}

std::vector<Arxiv::Article> DatabaseManager::GetRecent(int days) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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

std::optional<Arxiv::Article> DatabaseManager::GetArticle(const std::string& link) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS + " FROM articles WHERE link = ?";
    Stmt stmt(db, sql.c_str(), "GetArticle");
    stmt.bind(1, link);
    if (stmt.step() == SQLITE_ROW)
        return RowToArticle(stmt.raw());
    return std::nullopt;
}

std::vector<Arxiv::Article> DatabaseManager::ListBookmarked() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug("[Database]: Collecting all bookmarked articles");
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS +
                      " FROM articles WHERE bookmarked = 1 ORDER BY date DESC";
    std::vector<Article> articles;
    Stmt stmt(db, sql.c_str(), "ListBookmarked");
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    return articles;
}

void DatabaseManager::ToggleBookmark(const std::string& link, bool bookmarked) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug("[Database]: Toggling bookmark for {}", link);
    Stmt stmt(db, "UPDATE articles SET bookmarked = ? WHERE link = ?", "ToggleBookmark");
    stmt.bind(1, bookmarked ? 1 : 0).bind(2, link).step_done();
}

void DatabaseManager::DeleteArticle(const std::string& link) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug("[Database]: Deleting article {}", link);
    // Remember the deletion (and when the paper was first stored, for undo).
    Stmt mark(db,
              "INSERT OR REPLACE INTO deleted_articles (link, deleted_at, added_at) "
              "SELECT link, strftime('%s', 'now'), added_at FROM articles WHERE link = ?",
              "DeleteArticle/mark");
    mark.bind(1, link).step_done();
    // Memberships, ratings, notes and tags cascade.
    Stmt a(db, "DELETE FROM articles WHERE link = ?", "DeleteArticle");
    a.bind(1, link).step_done();
}

void DatabaseManager::MigrateAddReadAt() {
    AddColumnIfMissing("articles", "read_at", "INTEGER DEFAULT NULL");
}

void DatabaseManager::MigrateAddAddedAt() {
    AddColumnIfMissing("articles", "added_at", "INTEGER DEFAULT NULL");
    // Rows that predate the column have no insertion time on record; their
    // submission date is the best available stand-in. Idempotent: every row
    // written since the migration carries a non-NULL timestamp.
    ExecuteSQL("UPDATE articles SET added_at = date WHERE added_at IS NULL");
}

void DatabaseManager::MigrateAddUpdatedAt() {
    AddColumnIfMissing("articles", "updated_at", "INTEGER DEFAULT NULL");
    // No backfill: NULL is the correct value for every pre-existing row. The
    // column records when we saw a replacement announcement, and nothing on
    // record was ever observed as one, so guessing a timestamp here would put
    // the whole archive into the Updated view on the first run after upgrade.
}

void DatabaseManager::CreateTagTables() {
    ExecuteSQL(R"(CREATE TABLE IF NOT EXISTS tags (
               name TEXT PRIMARY KEY))");
    ExecuteSQL(link_table_sql(kLinkTables[3][1], "article_tags"));
}

void DatabaseManager::AddTag(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "INSERT OR IGNORE INTO tags (name) VALUES (?)", "AddTag");
    stmt.bind(1, name).step_done();
}

void DatabaseManager::RemoveTag(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt at(db, "DELETE FROM article_tags WHERE tag_name = ?", "RemoveTag/at");
    at.bind(1, name).step_done();
    Stmt t(db, "DELETE FROM tags WHERE name = ?", "RemoveTag");
    t.bind(1, name).step_done();
}

std::vector<std::string> DatabaseManager::GetTags() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> tags;
    Stmt stmt(db, "SELECT name FROM tags ORDER BY name", "GetTags");
    stmt.for_each([&](sqlite3_stmt* s) {
        const char* v = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        if (v)
            tags.emplace_back(v);
    });
    return tags;
}

DatabaseManager::FilterCounts DatabaseManager::CountFilters() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    FilterCounts counts;
    Stmt totals(db,
                "SELECT COALESCE(SUM(bookmarked = 1), 0), COALESCE(SUM(read_at IS NULL), 0) "
                "FROM articles",
                "CountFilters/totals");
    totals.for_each([&](sqlite3_stmt* s) {
        counts.bookmarked = sqlite3_column_int(s, 0);
        counts.unread = sqlite3_column_int(s, 1);
    });
    auto per_name = [&](const char* sql, const char* what, std::map<std::string, int>& out) {
        Stmt stmt(db, sql, what);
        stmt.for_each([&](sqlite3_stmt* s) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            if (name)
                out[std::string(name, static_cast<std::size_t>(sqlite3_column_bytes(s, 0)))] =
                    sqlite3_column_int(s, 1);
        });
    };
    per_name("SELECT t.name, COUNT(at.article_link) FROM tags t"
             " LEFT JOIN article_tags at ON at.tag_name = t.name GROUP BY t.name",
             "CountFilters/tags",
             counts.tags);
    per_name("SELECT p.name, COUNT(pa.article_link) FROM projects p"
             " LEFT JOIN project_articles pa ON pa.project_name = p.name GROUP BY p.name",
             "CountFilters/projects",
             counts.projects);
    return counts;
}

std::vector<std::string> DatabaseManager::GetTagsForArticle(const std::string& link) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> tags;
    Stmt stmt(db,
              "SELECT tag_name FROM article_tags WHERE article_link = ? ORDER BY tag_name",
              "GetTagsForArticle");
    stmt.bind(1, link);
    stmt.for_each([&](sqlite3_stmt* s) {
        const char* v = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        if (v)
            tags.emplace_back(v);
    });
    return tags;
}

void DatabaseManager::LinkArticleToTag(const std::string& link, const std::string& tag) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // Ensure the tag exists before linking.
    AddTag(tag);
    // A paper that is not stored is skipped (the keys would reject it).
    Stmt stmt(db,
              "INSERT OR IGNORE INTO article_tags (article_link, tag_name) "
              "SELECT ?1, ?2 WHERE EXISTS (SELECT 1 FROM articles WHERE link = ?1)",
              "LinkArticleToTag");
    stmt.bind(1, link).bind(2, tag).step_done();
}

void DatabaseManager::UnlinkArticleFromTag(const std::string& link, const std::string& tag) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db,
              "DELETE FROM article_tags WHERE article_link = ? AND tag_name = ?",
              "UnlinkArticleFromTag");
    stmt.bind(1, link).bind(2, tag).step_done();
}

std::vector<Arxiv::Article> DatabaseManager::GetArticlesForTag(const std::string& tag) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<Article> articles;
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS_A +
                      " FROM articles a"
                      " JOIN article_tags at ON a.link = at.article_link"
                      " WHERE at.tag_name = ? ORDER BY a.date DESC";
    Stmt stmt(db, sql.c_str(), "GetArticlesForTag");
    stmt.bind(1, tag);
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    return articles;
}

void DatabaseManager::MigrateAddProjectBibPath() {
    AddColumnIfMissing("projects", "bib_path", "TEXT DEFAULT ''");
}

std::string DatabaseManager::GetProjectBibPath(const std::string& project_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "SELECT bib_path FROM projects WHERE name = ?", "GetProjectBibPath");
    stmt.bind(1, project_name);
    if (stmt.step() == SQLITE_ROW) {
        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt.raw(), 0));
        return val ? val : "";
    }
    return "";
}

void DatabaseManager::SetProjectBibPath(const std::string& project_name, const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "UPDATE projects SET bib_path = ? WHERE name = ?", "SetProjectBibPath");
    stmt.bind(1, path).bind(2, project_name).step_done();
}

void DatabaseManager::MigrateAddFTS5() {
    // Check whether the FTS5 virtual table already exists.
    {
        Stmt check(db,
                   "SELECT value FROM metadata WHERE key = 'migration_fts5_v1'",
                   "MigrateAddFTS5/check");
        if (check.step() == SQLITE_ROW)
            return;
    }

    spdlog::info("[Database]: Building FTS5 full-text index");
    // One transaction: a crash half-way must not leave the triggers installed
    // without the back-fill (or, on retry, with the back-fill doubled).
    ExecuteSQL("BEGIN");
    try {

        // Content table: FTS5 mirrors the articles table, updated via triggers.
        ExecuteSQL(R"(CREATE VIRTUAL TABLE IF NOT EXISTS articles_fts
            USING fts5(title, authors, abstract,
                       content=articles, content_rowid=rowid))");

        // Triggers keep the FTS index in sync with articles.
        ExecuteSQL(R"(CREATE TRIGGER IF NOT EXISTS articles_ai
            AFTER INSERT ON articles BEGIN
                INSERT INTO articles_fts(rowid, title, authors, abstract)
                VALUES (new.rowid, new.title, new.authors, new.abstract);
            END)");

        ExecuteSQL(R"(CREATE TRIGGER IF NOT EXISTS articles_ad
            AFTER DELETE ON articles BEGIN
                INSERT INTO articles_fts(articles_fts, rowid, title, authors, abstract)
                VALUES ('delete', old.rowid, old.title, old.authors, old.abstract);
            END)");

        ExecuteSQL(R"(CREATE TRIGGER IF NOT EXISTS articles_au
            AFTER UPDATE ON articles BEGIN
                INSERT INTO articles_fts(articles_fts, rowid, title, authors, abstract)
                VALUES ('delete', old.rowid, old.title, old.authors, old.abstract);
                INSERT INTO articles_fts(rowid, title, authors, abstract)
                VALUES (new.rowid, new.title, new.authors, new.abstract);
            END)");

        // Populate FTS with existing articles.
        ExecuteSQL(R"(INSERT INTO articles_fts(rowid, title, authors, abstract)
            SELECT rowid, title, authors, abstract FROM articles)");

        Stmt mark(db,
                  "INSERT OR REPLACE INTO metadata (key, value) "
                  "VALUES ('migration_fts5_v1', 'done')",
                  "MigrateAddFTS5/mark");
        mark.step_done();
        ExecuteSQL("COMMIT");
    } catch (...) {
        ExecuteSQL("ROLLBACK");
        throw;
    }
    spdlog::info("[Database]: FTS5 index ready");
}

void DatabaseManager::MarkArticleRead(const std::string& link) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug("[Database]: Marking article read {}", link);
    Stmt stmt(
        db,
        "UPDATE articles SET read_at = strftime('%s', 'now') WHERE link = ? AND read_at IS NULL",
        "MarkArticleRead");
    stmt.bind(1, link).step_done();
}

std::vector<Arxiv::Article> DatabaseManager::GetUnreadArticles() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<Article> articles;
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS +
                      " FROM articles WHERE read_at IS NULL ORDER BY date DESC";
    Stmt stmt(db, sql.c_str(), "GetUnreadArticles");
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    return articles;
}

void DatabaseManager::PruneArticles(int max_age_days) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (max_age_days <= 0)
        return;
    spdlog::info("[Database]: Pruning articles older than {} days", max_age_days);
    Stmt stmt(db,
              "DELETE FROM articles "
              "WHERE date < strftime('%s', 'now') - ? * 86400 "
              "AND bookmarked = 0 "
              "AND link NOT IN (SELECT article_link FROM article_ratings) "
              "AND link NOT IN (SELECT article_link FROM project_articles) "
              "AND link NOT IN (SELECT article_link FROM article_tags) "
              "AND link NOT IN (SELECT article_link FROM project_notes)",
              "PruneArticles");
    stmt.bind(1, max_age_days).step_done();
}

void DatabaseManager::AddProject(const std::string& project_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // OR IGNORE, not OR REPLACE: REPLACE deletes the existing row and resets
    // its parent and bib_path to their defaults.
    Stmt stmt(db, "INSERT OR IGNORE INTO projects (name) VALUES (?)", "AddProject");
    stmt.bind(1, project_name).step_done();
}

void DatabaseManager::RemoveProject(const std::string& project_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
    // Memberships and notes cascade.
    bind_and_step("DELETE FROM projects WHERE name = ?", "RemoveProject:projects");
    bind_and_step("UPDATE projects SET parent = '' WHERE parent = ?", "RemoveProject:reparent");
    ExecuteSQL("COMMIT");
}

std::vector<std::string> DatabaseManager::GetProjects() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> projects;
    Stmt stmt(db, "SELECT name FROM projects", "GetProjects");
    stmt.for_each([&](sqlite3_stmt* s) {
        if (const auto* v = sqlite3_column_text(s, 0)) // NULL names are skipped
            projects.emplace_back(reinterpret_cast<const char*>(v));
    });
    return projects;
}

void DatabaseManager::LinkArticleToProject(const std::string& article_link,
                                           const std::string& project_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug("[Database]: Linking article {} to project {}", article_link, project_name);
    // Skipped unless both the project and the paper are stored.
    Stmt stmt(db,
              "INSERT OR IGNORE INTO project_articles (project_name, article_link) "
              "SELECT ?1, ?2 WHERE EXISTS (SELECT 1 FROM projects WHERE name = ?1) "
              "AND EXISTS (SELECT 1 FROM articles WHERE link = ?2)",
              "LinkArticleToProject");
    stmt.bind(1, project_name).bind(2, article_link).step_done();
}

void DatabaseManager::UnlinkArticleFromProject(const std::string& article_link,
                                               const std::string& project_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug("[Database]: Unlinking article {} from project {}", article_link, project_name);
    Stmt stmt(db,
              "DELETE FROM project_articles WHERE project_name = ? AND article_link = ?",
              "UnlinkArticleFromProject");
    stmt.bind(1, project_name).bind(2, article_link).step_done();
}

std::vector<Arxiv::Article>
DatabaseManager::GetArticlesForProject(const std::string& project_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug("[Database]: Collecting articles for project {}", project_name);
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS_A +
                      " FROM articles a JOIN project_articles pa ON a.link = pa.article_link "
                      "WHERE pa.project_name = ? ORDER BY a.date DESC";
    std::vector<Article> articles;
    Stmt stmt(db, sql.c_str(), "GetArticlesForProject");
    stmt.bind(1, project_name);
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    spdlog::debug("[Database]: Success. Found {} articles", articles.size());
    return articles;
}

std::string DatabaseManager::ExtractColumn(sqlite3_stmt* stmt, int index) {
    const unsigned char* text = sqlite3_column_text(stmt, index);
    if (!text)
        return {};
    // By length, not up to the first NUL.
    return std::string(reinterpret_cast<const char*>(text),
                       static_cast<std::size_t>(sqlite3_column_bytes(stmt, index)));
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
    article.read = (sqlite3_column_type(stmt, 8) != SQLITE_NULL);

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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug("[Database]: Getting projects for article {}", article_link);
    std::vector<std::string> projects;
    Stmt stmt(db,
              "SELECT project_name FROM project_articles WHERE article_link = ?",
              "GetProjectsForArticle");
    stmt.bind(1, article_link);
    stmt.for_each([&](sqlite3_stmt* s) {
        if (const auto* v = sqlite3_column_text(s, 0)) // NULL names are skipped
            projects.emplace_back(reinterpret_cast<const char*>(v));
    });
    return projects;
}

std::vector<Arxiv::Article> DatabaseManager::GetArticlesForDateRange(const std::string& start_date,
                                                                     const std::string& end_date) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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

    // Article dates are arXiv days stored as UTC instants: the range is
    // [start 00:00 UTC, the day after end 00:00 UTC), whatever the local
    // time zone (mktime here shifted it by the UTC offset and by DST).
    end_tm.tm_mday += 1;
    time_t start_time = timegm(&start_tm);
    time_t end_time = timegm(&end_tm);

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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug("[Database]: Setting rating {} for {}", rating, link);
    Stmt stmt(db,
              "INSERT OR REPLACE INTO article_ratings (article_link, rating) "
              "SELECT ?1, ?2 WHERE EXISTS (SELECT 1 FROM articles WHERE link = ?1)",
              "SetRating");
    stmt.bind(1, link).bind(2, rating).step_done();
}

int DatabaseManager::GetRating(const std::string& link) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "SELECT rating FROM article_ratings WHERE article_link = ?", "GetRating");
    stmt.bind(1, link);
    int rating = 0;
    if (stmt.step() == SQLITE_ROW) {
        rating = sqlite3_column_int(stmt.raw(), 0);
    }
    return rating;
}

DatabaseManager::RatedArticleList DatabaseManager::GetRatedArticles() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    RatedArticleList result;
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS_A +
                      ", r.rating "
                      "FROM articles a JOIN article_ratings r ON a.link = r.article_link";
    Stmt stmt(db, sql.c_str(), "GetRatedArticles");
    stmt.for_each([&](sqlite3_stmt* s) {
        Article article = RowToArticle(s);
        // ARTICLE_COLUMNS_A has 9 columns (0-8); rating is column 9.
        int rating = sqlite3_column_int(s, 9);
        result.emplace_back(std::move(article), rating);
    });
    spdlog::debug("[Database]: Found {} rated articles", result.size());
    return result;
}

std::string DatabaseManager::GetProjectParent(const std::string& project_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "SELECT COALESCE(parent, '') FROM projects WHERE name = ?", "GetProjectParent");
    stmt.bind(1, project_name);
    std::string parent;
    if (stmt.step() == SQLITE_ROW) {
        parent = ExtractColumn(stmt.raw(), 0);
    }
    return parent;
}

void DatabaseManager::SetProjectParent(const std::string& project_name, const std::string& parent) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "UPDATE projects SET parent = ? WHERE name = ?", "SetProjectParent");
    stmt.bind(1, parent).bind(2, project_name).step_done();
}

void DatabaseManager::SetProjectNote(const std::string& project_name,
                                     const std::string& article_link,
                                     const std::string& note) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    spdlog::debug(
        "[Database]: Setting note for article {} in project {}", article_link, project_name);
    Stmt stmt(db,
              "INSERT OR REPLACE INTO project_notes (project_name, article_link, note) "
              "SELECT ?1, ?2, ?3 WHERE EXISTS (SELECT 1 FROM projects WHERE name = ?1) "
              "AND EXISTS (SELECT 1 FROM articles WHERE link = ?2)",
              "SetProjectNote");
    stmt.bind(1, project_name).bind(2, article_link).bind(3, note).step_done();
}

std::string DatabaseManager::GetProjectNote(const std::string& project_name,
                                            const std::string& article_link) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<Article> articles;

    if (!search_title && !search_authors && !search_abstract) {
        spdlog::warn("[Database]: No search fields selected");
        return articles;
    }

    spdlog::debug("[Database]: FTS5 search '{}' title:{} authors:{} abstract:{}",
                  query,
                  search_title,
                  search_authors,
                  search_abstract);

    // Build the FTS5 column filter prefix for the MATCH expression.
    // When all three fields are searched, no prefix is needed (searches all).
    std::string col_filter;
    if (!(search_title && search_authors && search_abstract)) {
        std::string cols;
        if (search_title)
            cols += " title";
        if (search_authors)
            cols += " authors";
        if (search_abstract)
            cols += " abstract";
        col_filter = "{" + cols.substr(1) + "} : ";
    }

    // Wrap each token in double-quotes so FTS5 treats it as a phrase token,
    // not as an operator.  This makes multi-word queries behave like substring
    // matches rather than boolean AND.
    // Build a per-token AND query so that "quantum gravity" means
    // articles containing BOTH words, not either.
    // Control characters (NUL in particular) can never match and break the
    // FTS5 parser inside a quoted token; treat them as separators.
    std::string cleaned = query;
    for (char& c : cleaned)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            c = ' ';

    std::string fts_query;
    {
        std::istringstream iss(cleaned);
        std::string token;
        while (iss >> token) {
            if (!fts_query.empty())
                fts_query += " AND ";
            std::string escaped;
            for (char c : token) {
                if (c == '"')
                    escaped += '"';
                escaped += c;
            }
            fts_query += col_filter + '"' + escaped + '"';
        }
    }
    if (fts_query.empty())
        return articles;

    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS_A +
                      " FROM articles a"
                      " JOIN articles_fts ON articles_fts.rowid = a.rowid"
                      " WHERE articles_fts MATCH ?"
                      " ORDER BY bm25(articles_fts)";

    Stmt stmt(db, sql.c_str(), "SearchArticles");
    stmt.bind(1, fts_query);
    try {
        stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    } catch (const std::runtime_error& e) {
        // User input: a query FTS5 cannot parse finds nothing rather than
        // taking the UI down.
        spdlog::warn("[Database]: Search for '{}' failed: {}", query, e.what());
        articles.clear();
    }

    spdlog::debug("[Database]: Found {} articles", articles.size());
    return articles;
}

void DatabaseManager::SetRelevanceScore(const std::string& link, float score) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "UPDATE articles SET relevance_score = ? WHERE link = ?", "SetRelevanceScore");
    stmt.bind(1, static_cast<double>(score)).bind(2, link).step_done();
}

float DatabaseManager::GetRelevanceScore(const std::string& link) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "SELECT relevance_score FROM articles WHERE link = ?", "GetRelevanceScore");
    stmt.bind(1, link);
    float score = 0.0f;
    if (stmt.step() == SQLITE_ROW) {
        score = static_cast<float>(sqlite3_column_double(stmt.raw(), 0));
    }
    return score;
}

void DatabaseManager::FollowAuthor(const std::string& author_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(
        db, "INSERT OR IGNORE INTO followed_authors (author_name) VALUES (?)", "FollowAuthor");
    stmt.bind(1, author_name).step_done();
}

void DatabaseManager::UnfollowAuthor(const std::string& author_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "DELETE FROM followed_authors WHERE author_name = ?", "UnfollowAuthor");
    stmt.bind(1, author_name).step_done();
}

bool DatabaseManager::IsFollowingAuthor(const std::string& author_name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "SELECT 1 FROM followed_authors WHERE author_name = ?", "IsFollowingAuthor");
    stmt.bind(1, author_name);
    return stmt.step() == SQLITE_ROW;
}

std::vector<std::string> DatabaseManager::GetFollowedAuthors() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> authors;
    Stmt stmt(db, "SELECT author_name FROM followed_authors", "GetFollowedAuthors");
    stmt.for_each([&](sqlite3_stmt* s) {
        if (const auto* v = sqlite3_column_text(s, 0)) // NULL names are skipped
            authors.emplace_back(reinterpret_cast<const char*>(v));
    });
    return authors;
}

void DatabaseManager::SetMetadata(const std::string& key, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt stmt(db, "INSERT OR REPLACE INTO metadata (key, value) VALUES (?, ?)", "SetMetadata");
    stmt.bind(1, key).bind(2, value).step_done();
}

std::string DatabaseManager::GetMetadata(const std::string& key) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // Convert "YYYY-MM-DD" to UTC midnight Unix timestamp. The value comes
    // from the metadata table, so a malformed one means "no lower bound".
    std::tm tm{};
    char* end = strptime(utc_date.c_str(), "%Y-%m-%d", &tm);
    if (!end || *end != '\0') {
        spdlog::warn("[Database]: Ignoring malformed date '{}'", utc_date);
        return GetRecent(-1);
    }
    std::time_t since_ts = timegm(&tm);

    std::vector<Arxiv::Article> articles;
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS +
                      " FROM articles WHERE date >= ? ORDER BY date DESC";
    Stmt stmt(db, sql.c_str(), "GetArticlesSince");
    stmt.bind(1, static_cast<sqlite3_int64>(since_ts));
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    return articles;
}

std::vector<Arxiv::Article> DatabaseManager::GetArticlesAddedSince(std::int64_t since_ts) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<Arxiv::Article> articles;
    // Strictly greater: since_ts is the instant the previous fetch finished, so
    // rows carrying exactly that timestamp were stored *by* that fetch and the
    // user has already seen them. Both stamps have second granularity and the
    // end-of-fetch marker is written straight after the bulk insert, so the tail
    // of every fetch shares the anchor's second — with >= those papers were
    // replayed as new on the following day.
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS +
                      " FROM articles WHERE added_at > ? ORDER BY date DESC";
    Stmt stmt(db, sql.c_str(), "GetArticlesAddedSince");
    stmt.bind(1, static_cast<sqlite3_int64>(since_ts));
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    return articles;
}

std::vector<Arxiv::Article> DatabaseManager::GetArticlesUpdatedSince(std::int64_t since_ts) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<Arxiv::Article> articles;
    // Strictly greater, and ordered by the update itself rather than by `date`:
    // these are re-announcements of papers whose submission dates are spread
    // across years, so the only ordering the user cares about is most recently
    // updated first. See GetArticlesAddedSince for why the anchor is exclusive.
    std::string sql = std::string("SELECT ") + ARTICLE_COLUMNS +
                      " FROM articles WHERE updated_at > ? ORDER BY updated_at DESC, date DESC";
    Stmt stmt(db, sql.c_str(), "GetArticlesUpdatedSince");
    stmt.bind(1, static_cast<sqlite3_int64>(since_ts));
    stmt.for_each([&](sqlite3_stmt* s) { articles.push_back(RowToArticle(s)); });
    return articles;
}
