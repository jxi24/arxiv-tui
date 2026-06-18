// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"
#include "Arxiv/Article.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>

#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"

using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock = arxiv_tui::test::FetcherMock;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string today_utc() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

// Returns UTC midnight (as time_t) for a "YYYY-MM-DD" string.
static std::time_t utc_midnight(const std::string& date) {
    std::tm tm{};
    tm.tm_year = std::stoi(date.substr(0, 4)) - 1900;
    tm.tm_mon = std::stoi(date.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(date.substr(8, 2));
    return timegm(&tm);
}

// ---------------------------------------------------------------------------
// DatabaseManager: metadata (real SQLite, in-memory)
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::SetMetadata / GetMetadata: round-trip", "[newart][db]") {
    Arxiv::DatabaseManager db(":memory:");
    db.SetMetadata("last_fetch_date", "2026-05-01");
    REQUIRE(db.GetMetadata("last_fetch_date") == "2026-05-01");
}

TEST_CASE("DatabaseManager::GetMetadata: returns empty string for unknown key", "[newart][db]") {
    Arxiv::DatabaseManager db(":memory:");
    REQUIRE(db.GetMetadata("nonexistent_key") == "");
}

TEST_CASE("DatabaseManager::SetMetadata: overwrites previous value", "[newart][db]") {
    Arxiv::DatabaseManager db(":memory:");
    db.SetMetadata("last_fetch_date", "2026-05-01");
    db.SetMetadata("last_fetch_date", "2026-05-04");
    REQUIRE(db.GetMetadata("last_fetch_date") == "2026-05-04");
}

// ---------------------------------------------------------------------------
// DatabaseManager: GetArticlesSince (real SQLite, in-memory)
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::GetArticlesSince: returns articles on or after the date",
          "[newart][db]") {
    Arxiv::DatabaseManager db(":memory:");

    Arxiv::Article old_article;
    old_article.link = "https://arxiv.org/abs/old";
    old_article.title = "Old Article";
    old_article.authors = "Author A";
    old_article.abstract = "Abstract old";
    old_article.date = std::chrono::system_clock::from_time_t(utc_midnight("2026-05-01"));
    db.AddArticle(old_article);

    Arxiv::Article new_article;
    new_article.link = "https://arxiv.org/abs/new";
    new_article.title = "New Article";
    new_article.authors = "Author B";
    new_article.abstract = "Abstract new";
    new_article.date = std::chrono::system_clock::from_time_t(utc_midnight("2026-05-04"));
    db.AddArticle(new_article);

    auto results = db.GetArticlesSince("2026-05-04");
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].link == "https://arxiv.org/abs/new");
}

TEST_CASE("DatabaseManager::GetArticlesSince: returns empty when no articles match",
          "[newart][db]") {
    Arxiv::DatabaseManager db(":memory:");

    Arxiv::Article article;
    article.link = "https://arxiv.org/abs/old";
    article.title = "Old Article";
    article.authors = "Author A";
    article.abstract = "Abstract";
    article.date = std::chrono::system_clock::from_time_t(utc_midnight("2026-05-01"));
    db.AddArticle(article);

    auto results = db.GetArticlesSince("2026-05-05");
    REQUIRE(results.empty());
}

TEST_CASE("DatabaseManager::GetArticlesSince: returns multiple articles from that day",
          "[newart][db]") {
    Arxiv::DatabaseManager db(":memory:");

    for (int day = 1; day <= 5; ++day) {
        Arxiv::Article a;
        a.link = "https://arxiv.org/abs/art" + std::to_string(day);
        a.title = "Article " + std::to_string(day);
        a.date =
            std::chrono::system_clock::from_time_t(utc_midnight("2026-05-0" + std::to_string(day)));
        db.AddArticle(a);
    }

    auto results = db.GetArticlesSince("2026-05-03");
    REQUIRE(results.size() == 3);
}

// ---------------------------------------------------------------------------
// AppCore: "New Articles" appears in filter options
// ---------------------------------------------------------------------------

TEST_CASE("AppCore: filter options contain 'New Articles'", "[newart][appcore]") {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    auto opts = core->GetFilterOptions();
    bool found = std::find(opts.begin(), opts.end(), "New Articles") != opts.end();
    REQUIRE(found);
}

TEST_CASE("AppCore: FilterView::NewArticles value is less than FilterView::Project",
          "[newart][appcore]") {
    REQUIRE(static_cast<int>(Arxiv::AppCore::FilterView::NewArticles) <
            static_cast<int>(Arxiv::AppCore::FilterView::Project));
}

// ---------------------------------------------------------------------------
// AppCore: NewArticles filter dispatches to GetArticlesSince
// ---------------------------------------------------------------------------

TEST_CASE("AppCore: NewArticles filter returns articles from GetArticlesSince",
          "[newart][appcore]") {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    auto* db_raw = db_ptr.get();

    // Override GetMetadata to simulate a previous fetch date
    ALLOW_CALL(*db_raw, GetMetadata(std::string("last_fetch_date")))
        .RETURN(std::string("2026-05-01"));

    Arxiv::Article new_art;
    new_art.link = "https://arxiv.org/abs/new";
    new_art.title = "New Paper";
    new_art.authors = "Alice";
    new_art.date = std::chrono::system_clock::now();
    ALLOW_CALL(*db_raw, GetArticlesSince(trompeloeil::_))
        .RETURN(std::vector<Arxiv::Article>{new_art});

    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    core->SetFilterIndex(Arxiv::AppCore::FilterView::NewArticles);
    auto articles = core->GetCurrentArticles();
    REQUIRE(articles.size() == 1);
    REQUIRE(articles[0].link == "https://arxiv.org/abs/new");
}

// ---------------------------------------------------------------------------
// AppCore: last_fetch_date is written after startup fetch
// ---------------------------------------------------------------------------

TEST_CASE("AppCore: SetMetadata is called with today's UTC date on construction",
          "[newart][appcore]") {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    auto* db_raw = db_ptr.get();

    bool set_metadata_called = false;
    std::string stored_date;

    ALLOW_CALL(*db_raw, SetMetadata(std::string("last_fetch_date"), trompeloeil::_))
        .LR_SIDE_EFFECT({
            set_metadata_called = true;
            stored_date = _2;
        });

    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    REQUIRE(set_metadata_called);
    REQUIRE(stored_date == today_utc());
}

// ---------------------------------------------------------------------------
// AppCore: GetFilterView returns NewArticles when set
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::GetFilterView returns NewArticles when set", "[newart][appcore]") {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    core->SetFilterIndex(Arxiv::AppCore::FilterView::NewArticles);
    REQUIRE(core->GetFilterView() == Arxiv::AppCore::FilterView::NewArticles);
}

// ---------------------------------------------------------------------------
// AppCore: Project indices are unaffected (still >= FilterView::Project)
// ---------------------------------------------------------------------------

TEST_CASE("AppCore: project filter indices are still above NewArticles", "[newart][appcore]") {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    auto* db_raw = db_ptr.get();

    ALLOW_CALL(*db_raw, GetProjects()).RETURN(std::vector<std::string>{"MyProject"});

    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    // The project filter index must be > NewArticles index
    int new_articles_idx = static_cast<int>(Arxiv::AppCore::FilterView::NewArticles);
    auto opts = core->GetFilterOptions();
    auto it = std::find(opts.begin(), opts.end(), "MyProject");
    REQUIRE(it != opts.end());
    int proj_idx = static_cast<int>(std::distance(opts.begin(), it));
    REQUIRE(proj_idx > new_articles_idx);
    REQUIRE(core->GetProjectNameForFilter(proj_idx) == "MyProject");
}

// ---------------------------------------------------------------------------
// Fetcher: FetchSince is on the interface and mockable
// ---------------------------------------------------------------------------

TEST_CASE("FetcherMock: FetchSince is mockable and returns empty by default", "[newart][fetcher]") {
    FetcherMock mock;
    auto result = mock.FetchSince("2026-05-01");
    REQUIRE(result.empty());
}

// ---------------------------------------------------------------------------
// Default view: AppCore opens on NewArticles, not All
// ---------------------------------------------------------------------------

TEST_CASE("AppCore: default filter view is NewArticles", "[newart][appcore]") {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    REQUIRE(core->GetFilterView() == Arxiv::AppCore::FilterView::NewArticles);
}

TEST_CASE("AppCore: default filter index equals NewArticles integer value", "[newart][appcore]") {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    REQUIRE(core->GetFilterIndex() == static_cast<int>(Arxiv::AppCore::FilterView::NewArticles));
}

// ---------------------------------------------------------------------------
// Posted date: AppCore calls FetchSince(prev_date), not FetchSince(prev_date+1)
// ---------------------------------------------------------------------------

TEST_CASE("AppCore: FetchSince is called with the previous fetch date (posted-date semantics)",
          "[newart][appcore]") {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    auto* db_raw = db_ptr.get();
    auto* fet_raw = fet_ptr.get();

    const std::string prev = "2026-05-01";

    ALLOW_CALL(*db_raw, GetMetadata(std::string("last_fetch_date"))).RETURN(prev);

    // FetchSince must be called with exactly prev_date, not prev_date+1.
    bool fetch_since_called = false;
    std::string fetch_since_arg;
    ALLOW_CALL(*fet_raw, FetchSince(trompeloeil::_))
        .LR_SIDE_EFFECT({
            fetch_since_called = true;
            fetch_since_arg = _1;
        })
        .RETURN(std::vector<Arxiv::Article>{});

    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    REQUIRE(fetch_since_called);
    REQUIRE(fetch_since_arg == prev);
}

// ---------------------------------------------------------------------------
// New Articles anchor: same-day restart must not collapse the "new" window
// ---------------------------------------------------------------------------
TEST_CASE("AppCore: same-day restart preserves the New Articles anchor", "[newart][appcore]") {
    // Scenario: yesterday the user fetched (last_fetch_date=yesterday).
    // Today's first run sets new_articles_anchor=yesterday and last_fetch_date=today.
    // Today's SECOND run (same-day restart) must NOT overwrite the anchor —
    // otherwise NewArticles asks GetArticlesSince(today+1) which is empty.
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    auto* db_raw = db_ptr.get();

    const std::string today_str = today_utc();
    const std::string yesterday = "2026-05-01"; // any date < today

    // Simulate: this is the same-day SECOND run. last_fetch_date already == today,
    // and new_articles_anchor was previously set to "yesterday".
    // Note: trompeloeil matches expectations in REVERSE registration order, so
    // the wildcard fallback must be registered first and the specific keys last.
    ALLOW_CALL(*db_raw, GetMetadata(trompeloeil::_)).RETURN(std::string{});
    ALLOW_CALL(*db_raw, GetMetadata(std::string("last_fetch_date"))).RETURN(today_str);
    ALLOW_CALL(*db_raw, GetMetadata(std::string("new_articles_anchor"))).RETURN(yesterday);

    // The anchor must NOT be reset to today on this same-day restart.
    FORBID_CALL(*db_raw,
                SetMetadata(std::string("new_articles_anchor"), trompeloeil::eq(today_str)));

    // GetArticlesSince should be called with the day AFTER the anchor (= "2026-05-02"),
    // not with the day after today. On a same-day restart articles from the first
    // run ARE in the DB, so return a non-empty result so the fallback doesn't fire.
    Arxiv::Article existing;
    existing.link = "https://arxiv.org/abs/existing";
    existing.title = "Existing Paper";
    existing.date = std::chrono::system_clock::now();

    std::string first_articles_since_arg;
    ALLOW_CALL(*db_raw, GetArticlesSince(trompeloeil::_))
        .LR_SIDE_EFFECT(if (first_articles_since_arg.empty()) first_articles_since_arg = _1;)
        .RETURN(std::vector<Arxiv::Article>{existing});

    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    core->SetFilterIndex(Arxiv::AppCore::FilterView::NewArticles);
    REQUIRE(first_articles_since_arg == "2026-05-02");
}

TEST_CASE("AppCore: NewArticles excludes replacements (updated papers)", "[newart][appcore]") {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    auto* db_raw = db_ptr.get();

    const std::string yesterday = "2026-05-01";

    ALLOW_CALL(*db_raw, GetMetadata(trompeloeil::_)).RETURN(std::string{});
    ALLOW_CALL(*db_raw, GetMetadata(std::string("last_fetch_date"))).RETURN(yesterday);
    ALLOW_CALL(*db_raw, GetMetadata(std::string("new_articles_anchor"))).RETURN(yesterday);

    Arxiv::Article original;
    original.link = "https://arxiv.org/abs/2401.12345";
    original.title = "Original Submission";
    original.is_replacement = false;
    original.date = std::chrono::system_clock::now();

    Arxiv::Article update;
    update.link = "https://arxiv.org/abs/2400.99999";
    update.title = "An Updated Paper";
    update.is_replacement = true;
    update.date = std::chrono::system_clock::now();

    ALLOW_CALL(*db_raw, GetArticlesSince(trompeloeil::_))
        .RETURN(std::vector<Arxiv::Article>{original, update});

    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    core->SetFilterIndex(Arxiv::AppCore::FilterView::NewArticles);
    auto current = core->GetCurrentArticles();
    REQUIRE(current.size() == 1);
    REQUIRE(current[0].link == original.link);
}

// ---------------------------------------------------------------------------
// First-daily-open bug: NewArticles must not be blank while today's articles
// are not yet in the DB. Falls back to showing anchor-date articles.
// ---------------------------------------------------------------------------

TEST_CASE("AppCore: NewArticles shows anchor-date articles when today's query returns empty",
          "[newart][appcore]") {
    // Scenario: first daily open. prev_fetch = 2026-05-01 (yesterday).
    // DB only holds yesterday's articles (date = 2026-05-01).
    // The advanced query for 2026-05-02 returns empty.
    // AppCore must fall back to GetArticlesSince(anchor="2026-05-01")
    // so the view is not blank.
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    auto* db_raw = db_ptr.get();

    const std::string prev_fetch = "2026-05-01";
    const std::string advanced_date = "2026-05-02"; // anchor + 1

    ALLOW_CALL(*db_raw, GetMetadata(trompeloeil::_)).RETURN(std::string{});
    ALLOW_CALL(*db_raw, GetMetadata(std::string("last_fetch_date"))).RETURN(prev_fetch);

    Arxiv::Article anchor_article;
    anchor_article.link = "https://arxiv.org/abs/yesterday";
    anchor_article.title = "Yesterday Paper";
    anchor_article.date = std::chrono::system_clock::from_time_t(utc_midnight(prev_fetch));

    // Use a side-effecting ALLOW_CALL: return articles for anchor date, empty for advanced.
    ALLOW_CALL(*db_raw, GetArticlesSince(trompeloeil::_))
        .LR_RETURN((_1 == advanced_date) ? std::vector<Arxiv::Article>{}
                                         : std::vector<Arxiv::Article>{anchor_article});

    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    core->SetFilterIndex(Arxiv::AppCore::FilterView::NewArticles);
    auto articles = core->GetCurrentArticles();
    REQUIRE_FALSE(articles.empty());
    REQUIRE(articles[0].link == "https://arxiv.org/abs/yesterday");
}

// ---------------------------------------------------------------------------
// First-daily-open bug: the day-boundary path fetches today's articles too.
//
// On a new-day first open the day's freshly-announced papers must be fetched
// immediately — otherwise they only appear after a *second* open. FetchSince
// owns that responsibility: it backfills the missed days AND folds in today's
// announcement, so AppCore issues a single fetcher call and must NOT make a
// separate Fetch() on this path.
// ---------------------------------------------------------------------------
TEST_CASE("AppCore: first daily open fetches via FetchSince alone", "[newart][appcore]") {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    auto* db_raw = db_ptr.get();
    auto* fet_raw = fet_ptr.get();

    // Previous fetch was a strictly-earlier day → day-boundary crossing.
    ALLOW_CALL(*db_raw, GetMetadata(std::string("last_fetch_date")))
        .RETURN(std::string("2026-05-01"));

    bool fetch_since_called = false;
    ALLOW_CALL(*fet_raw, FetchSince(trompeloeil::_))
        .LR_SIDE_EFFECT(fetch_since_called = true;)
        .RETURN(std::vector<Arxiv::Article>{});
    // No separate RSS fetch: FetchSince includes today's articles itself.
    FORBID_CALL(*fet_raw, Fetch());

    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    REQUIRE(fetch_since_called);
}

TEST_CASE("AppCore: day-boundary crossing advances the New Articles anchor", "[newart][appcore]") {
    // Scenario: last_fetch_date is some date strictly before today, and no
    // anchor is persisted yet. We must set the anchor to that previous date.
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    auto* db_raw = db_ptr.get();

    const std::string yesterday = "2026-05-01";

    ALLOW_CALL(*db_raw, GetMetadata(trompeloeil::_)).RETURN(std::string{});
    ALLOW_CALL(*db_raw, GetMetadata(std::string("last_fetch_date"))).RETURN(yesterday);
    ALLOW_CALL(*db_raw, GetMetadata(std::string("new_articles_anchor"))).RETURN(std::string{});

    bool anchor_set = false;
    std::string anchor_value;
    ALLOW_CALL(*db_raw, SetMetadata(std::string("new_articles_anchor"), trompeloeil::_))
        .LR_SIDE_EFFECT({
            anchor_set = true;
            anchor_value = _2;
        });

    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    REQUIRE(anchor_set);
    REQUIRE(anchor_value == yesterday);
}
