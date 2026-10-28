// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <thread>

#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"

using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock = arxiv_tui::test::FetcherMock;

// ---------------------------------------------------------------------------
// Config::auto_refresh_minutes
// ---------------------------------------------------------------------------

TEST_CASE("Config::auto_refresh_minutes: default is 0 (disabled)", "[refresh][config]") {
    Arxiv::Config cfg;
    REQUIRE(cfg.get_auto_refresh_minutes() == 0);
}

TEST_CASE("Config::set_auto_refresh_minutes: getter returns set value", "[refresh][config]") {
    Arxiv::Config cfg;
    cfg.set_auto_refresh_minutes(30);
    REQUIRE(cfg.get_auto_refresh_minutes() == 30);
}

// ---------------------------------------------------------------------------
// AppCore: StartAutoRefresh / StopAutoRefresh
// ---------------------------------------------------------------------------

static std::unique_ptr<Arxiv::AppCore>
make_core(DatabaseManagerMock*& db_out, FetcherMock*& fetcher_out, int refresh_minutes = 0) {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    db_out = db_ptr.get();
    fetcher_out = fet_ptr.get();
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    cfg.set_auto_refresh_minutes(refresh_minutes);
    return std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));
}

TEST_CASE("AppCore::IsAutoRefreshing: returns false before StartAutoRefresh",
          "[refresh][appcore]") {
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core(db, fetcher);

    REQUIRE_FALSE(core->IsAutoRefreshing());
}

TEST_CASE("AppCore::StartAutoRefresh / StopAutoRefresh: toggle correctly", "[refresh][appcore]") {
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core(db, fetcher, 60); // 60-minute interval

    REQUIRE_FALSE(core->IsAutoRefreshing());

    core->StartAutoRefresh();
    REQUIRE(core->IsAutoRefreshing());

    core->StopAutoRefresh();
    REQUIRE_FALSE(core->IsAutoRefreshing());
}

TEST_CASE("AppCore::StartAutoRefresh: is idempotent", "[refresh][appcore]") {
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core(db, fetcher, 60);

    core->StartAutoRefresh();
    core->StartAutoRefresh(); // second call should be a no-op
    REQUIRE(core->IsAutoRefreshing());

    core->StopAutoRefresh();
}

TEST_CASE("AppCore::StopAutoRefresh: is safe to call when not running", "[refresh][appcore]") {
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core(db, fetcher);

    REQUIRE_NOTHROW(core->StopAutoRefresh());
    REQUIRE_FALSE(core->IsAutoRefreshing());
}

TEST_CASE("AppCore destructor: auto-refresh thread is joined cleanly", "[refresh][appcore]") {
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    const auto start = std::chrono::steady_clock::now();
    {
        auto core = make_core(db, fetcher, 60);
        core->StartAutoRefresh();
        // The destructor stops the thread at once, not after the 60-minute
        // interval it is sleeping through.
    }
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
}

TEST_CASE("AppCore::GetAutoRefreshMinutes: returns configured value",
          "[refresh][config][appcore]") {
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core(db, fetcher, 15);
    REQUIRE(core->GetAutoRefreshMinutes() == 15);
}

// ---------------------------------------------------------------------------
// Regressions found in code review — each fails until the bug is fixed.
// ---------------------------------------------------------------------------

TEST_CASE("AppCore regression: auto-refresh fetches from the network and leaves the view "
          "to the UI thread",
          "[refresh][appcore][regression]") {
    // The refresh thread used to call FetchArticles(), rewriting the article
    // list the UI was reading, and never fetched anything new. It must fetch,
    // store, and only flag the UI thread to re-query.
    auto db_ptr = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    auto fet_ptr = std::make_unique<FetcherMock>();
    auto* fetcher = fet_ptr.get();
    Arxiv::Article fresh("Fresh",
                         "https://arxiv.org/abs/2610.00001",
                         "Abstract",
                         "A. Author",
                         std::chrono::system_clock::now(),
                         "cs.AI");
    std::atomic<int> fetches{0};
    // The constructor's initial fetch finds nothing; later fetches find `fresh`.
    ALLOW_CALL(*fetcher, Fetch())
        .LR_SIDE_EFFECT(++fetches)
        .LR_RETURN(fetches.load() > 1 ? std::vector<Arxiv::Article>{fresh}
                                      : std::vector<Arxiv::Article>{});
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    cfg.set_ranker_file("/tmp/arxiv_refresh_regression_ranker.bin");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));
    core->SetFilterIndex(Arxiv::AppCore::FilterView::All);
    const auto before = core->GetCurrentArticles().size();
    const int initial_fetches = fetches.load();

    core->StartAutoRefresh(std::chrono::milliseconds(20));
    for (int i = 0; i < 200 && fetches.load() == initial_fetches; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    core->StopAutoRefresh();

    SECTION("the refresh thread fetched") { REQUIRE(fetches.load() > initial_fetches); }
    SECTION("the view is untouched until the UI thread refreshes") {
        REQUIRE(core->GetCurrentArticles().size() == before);
    }
    SECTION("the UI thread picks the new article up") {
        core->TryRefetchIfNeeded();
        REQUIRE(core->GetCurrentArticles().size() == before + 1);
    }
}

TEST_CASE("AppCore::TryRefetchIfNeeded reports whether the view changed", "[refresh][appcore]") {
    // The UI redraws only when this (or an animation) says something changed,
    // instead of 20 times a second while idle.
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core(db, fetcher);

    SECTION("idle") { REQUIRE_FALSE(core->TryRefetchIfNeeded()); }
    SECTION("after a background refresh") {
        core->StartAutoRefresh(std::chrono::milliseconds(10));
        bool changed = false;
        for (int i = 0; i < 200 && !changed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            changed = core->TryRefetchIfNeeded();
        }
        core->StopAutoRefresh();
        REQUIRE(changed);
    }
}
