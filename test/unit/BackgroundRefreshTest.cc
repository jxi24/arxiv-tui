// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"

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
    {
        auto core = make_core(db, fetcher, 60);
        core->StartAutoRefresh();
        // Let the destructor stop the thread — should not hang or crash
    }
    REQUIRE(true); // reached here without deadlock
}

TEST_CASE("AppCore::GetAutoRefreshMinutes: returns configured value",
          "[refresh][config][appcore]") {
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core(db, fetcher, 15);
    REQUIRE(core->GetAutoRefreshMinutes() == 15);
}
