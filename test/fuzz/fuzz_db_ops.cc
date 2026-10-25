// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Stateful fuzzing of DatabaseManager: a byte stream becomes a sequence of
// add / delete / tag / project / note / rating / prune / search operations.
// After every run: no table references an article that was deleted (or
// pruned) and not re-added, and the FTS index passes its integrity check.

#include "Arxiv/DatabaseManager.hh"

#include <chrono>
#include <set>
#include <sqlite3.h>

#include "FuzzSupport.hh"

namespace {

const char* kLinks[] = {"https://arxiv.org/abs/2401.00001",
                        "https://arxiv.org/abs/2401.00002",
                        "https://arxiv.org/abs/2401.00003",
                        "https://arxiv.org/abs/hep-ph/0101001"};
const char* kNames[] = {"alpha", "beta", "", "gamma's"};

long long count(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
        arxiv_fuzz::fail("invariant query failed to prepare");
    long long n = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int64(st, 0) : -1;
    sqlite3_finalize(st);
    return n;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    arxiv_fuzz::quiet_logs();
    const auto path = (arxiv_fuzz::scratch_dir() / "ops.db").string();
    std::filesystem::remove(path);
    arxiv_fuzz::Bytes in(data, size);
    {
        Arxiv::DatabaseManager db(path);
        std::set<std::string> gone; // deleted and not re-added
        for (int step = 0; step < 64 && !in.empty(); ++step) {
            const std::uint8_t op = in.byte();
            const std::string link = kLinks[in.byte() % 4];
            const std::string name = kNames[in.byte() % 4];
            switch (op % 12) {
            case 0: {
                // Read in a fixed order: argument evaluation order differs
                // between compilers, and the corpus must mean the same thing.
                const std::string title = in.string(16);
                const std::string abstract = in.string(16);
                const auto day = in.byte();
                const bool replacement = in.byte() & 1;
                Arxiv::Article a("T " + title,
                                 link,
                                 "A " + abstract,
                                 "Au",
                                 std::chrono::system_clock::from_time_t(day * 86400LL),
                                 "hep-ph");
                a.is_replacement = replacement;
                db.AddArticle(a);
                gone.erase(link);
                break;
            }
            case 1:
                db.DeleteArticle(link);
                gone.insert(link);
                break;
            case 2:
                db.LinkArticleToTag(link, name);
                gone.erase(link); // a new reference, made deliberately
                break;
            case 3:
                db.AddProject(name);
                db.LinkArticleToProject(link, name);
                gone.erase(link); // a new reference, made deliberately
                break;
            case 4:
                db.SetProjectNote(name, link, in.string(16));
                gone.erase(link); // a new reference, made deliberately
                break;
            case 5:
                db.SetRating(link, 1 + in.byte() % 5);
                gone.erase(link); // a new reference, made deliberately
                break;
            case 6: {
                // Anything pruning removes counts as deleted.
                std::set<std::string> before;
                for (const auto& a : db.GetRecent(-1))
                    before.insert(a.link);
                db.PruneArticles(1 + in.byte() % 3);
                std::set<std::string> after;
                for (const auto& a : db.GetRecent(-1))
                    after.insert(a.link);
                for (const auto& l : before)
                    if (!after.count(l))
                        gone.insert(l);
                break;
            }
            case 7:
                db.ToggleBookmark(link, in.byte() & 1);
                break;
            case 8:
                db.MarkArticleRead(link);
                break;
            case 9: {
                const std::string query = in.string(24);
                const bool title = in.byte() & 1;
                const bool authors = in.byte() & 1;
                (void)db.SearchArticles(query, title, authors, true);
                break;
            }
            case 10:
                db.RemoveProject(name);
                break;
            case 11:
                db.RemoveTag(name);
                break;
            }
        }

        // A delete or prune must not leave rows behind. (References added
        // afterwards take the link out of `gone`.) Check with a second
        // connection.
        sqlite3* raw = nullptr;
        sqlite3_open(path.c_str(), &raw);
        for (const auto& l : gone) {
            for (const char* table :
                 {"article_tags", "project_articles", "project_notes", "article_ratings"}) {
                const std::string sql = std::string("SELECT count(*) FROM ") + table +
                                        " WHERE article_link = '" + l + "'";
                if (count(raw, sql) != 0) {
                    std::fprintf(stderr, "orphan in %s for %s\n", table, l.c_str());
                    arxiv_fuzz::fail("rows reference a deleted article");
                }
            }
        }
        FUZZ_CHECK(sqlite3_exec(raw,
                                "INSERT INTO articles_fts(articles_fts) VALUES('integrity-check')",
                                nullptr,
                                nullptr,
                                nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    return 0;
}
