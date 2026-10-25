// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Long-running exports (Obsidian, selected digest) and PDF downloads must not
// freeze the TUI: they run as a background task and report progress that the
// UI can show while the user keeps working.

#include "Arxiv/App.hh"
#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/KeyBindings.hh"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <unistd.h>
#include <vector>

#include "fixtures/test_data.hh"
#include "mocks/FetcherMock.hh"

using namespace Catch::Matchers;
using FetcherMock = arxiv_tui::test::FetcherMock;
using arxiv_tui::test::fixtures::sample_articles;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

// Holds every DownloadPaper call until the test opens the gate. A safety
// timeout lets a still-synchronous implementation fail the timing checks
// instead of hanging the suite.
struct DownloadGate {
    std::promise<void> release;
    std::shared_future<void> released{release.get_future().share()};
    std::atomic<int> calls{0};
    bool open = false;

    bool Wait() {
        ++calls;
        released.wait_for(2s);
        return true;
    }
    void Open() {
        if (!open) {
            open = true;
            release.set_value();
        }
    }
};

// Declared after the AppCore/ArxivApp so it is destroyed first: opening the
// gate lets the destructor's join finish promptly even when a check failed.
struct GateOpener {
    DownloadGate& gate;
    ~GateOpener() { gate.Open(); }
};

fs::path unique_temp(const std::string& stem) {
    static int counter = 0;
    return fs::temp_directory_path() /
           (stem + "_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
}

Arxiv::Config task_config() {
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI", "math.PR"});
    auto download_dir = unique_temp("arxiv_bg_downloads");
    fs::create_directories(download_dir);
    cfg.set_download_dir(download_dir.string());
    auto vault = unique_temp("arxiv_bg_vault");
    fs::remove_all(vault);
    cfg.set_obsidian_vault(vault.string());
    cfg.set_obsidian_download_pdfs(true);
    cfg.set_ranker_file((unique_temp("arxiv_bg_ranker") += ".bin").string());
    return cfg;
}

std::unique_ptr<Arxiv::DatabaseManager> seeded_db() {
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    for (const auto& a : sample_articles)
        db->AddArticle(a);
    return db;
}

// Fixture: AppCore over a real in-memory DB whose fetcher blocks downloads.
struct BlockedCore {
    DownloadGate gate;
    std::unique_ptr<Arxiv::AppCore> core;
    std::unique_ptr<trompeloeil::expectation> download;

    BlockedCore() {
        auto fetcher = std::make_unique<FetcherMock>();
        download = NAMED_ALLOW_CALL(*fetcher, DownloadPaper(ANY(std::string), ANY(std::string)))
                       .SIDE_EFFECT(gate.Wait())
                       .RETURN(true);
        core = std::make_unique<Arxiv::AppCore>(task_config(), seeded_db(), std::move(fetcher));
        core->SetFilterIndex(Arxiv::AppCore::FilterView::All);
    }
    ~BlockedCore() {
        gate.Open();
        core.reset(); // joins any task before the expectation goes away
    }

    void SelectBoth() {
        core->ToggleSelection(sample_articles[0].link);
        core->ToggleSelection(sample_articles[1].link);
    }

    // Time a call that should only *start* work.
    template <typename F> std::chrono::steady_clock::duration Time(F&& f) {
        auto t0 = std::chrono::steady_clock::now();
        f();
        return std::chrono::steady_clock::now() - t0;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Obsidian export
// ---------------------------------------------------------------------------

TEST_CASE("Background task: Obsidian export returns before the PDF downloads finish",
          "[background][obsidian]") {
    BlockedCore t;
    t.SelectBoth();

    bool started = false;
    auto elapsed = t.Time([&] { started = t.core->StartExportSelectedToObsidian(); });

    REQUIRE(started);
    REQUIRE(elapsed < 1s);
}

TEST_CASE("Background task: Obsidian export reports progress while running",
          "[background][obsidian]") {
    BlockedCore t;
    t.SelectBoth();
    REQUIRE(t.core->StartExportSelectedToObsidian());

    auto status = t.core->GetBackgroundTaskStatus();

    SECTION("running") { REQUIRE(status.running); }
    SECTION("labelled") { REQUIRE_THAT(status.label, ContainsSubstring("Obsidian")); }
    SECTION("total is the selection size") { REQUIRE(status.total == 2); }
    SECTION("nothing finished while downloads are held") { REQUIRE(status.done == 0); }
}

TEST_CASE("Background task: Obsidian export reports the index note when finished",
          "[background][obsidian]") {
    BlockedCore t;
    t.SelectBoth();
    REQUIRE(t.core->StartExportSelectedToObsidian());

    t.gate.Open();
    t.core->WaitForBackgroundTask();
    auto status = t.core->GetBackgroundTaskStatus();

    SECTION("no longer running") { REQUIRE_FALSE(status.running); }
    SECTION("every article done") { REQUIRE(status.done == 2); }
    SECTION("no error") { REQUIRE(status.error.empty()); }
    SECTION("result is the written index note") {
        REQUIRE_FALSE(status.result.empty());
        REQUIRE(fs::exists(status.result));
    }
}

TEST_CASE("Background task: a second task cannot start while one is running", "[background]") {
    BlockedCore t;
    t.SelectBoth();
    REQUIRE(t.core->StartExportSelectedToObsidian());

    REQUIRE_FALSE(t.core->StartExportSelectedToObsidian());
}

TEST_CASE("Background task: changing the selection does not affect a running export",
          "[background][obsidian]") {
    BlockedCore t;
    t.SelectBoth();
    REQUIRE(t.core->StartExportSelectedToObsidian());

    t.core->ClearSelections();
    t.gate.Open();
    t.core->WaitForBackgroundTask();

    REQUIRE(t.core->GetBackgroundTaskStatus().done == 2);
}

// ---------------------------------------------------------------------------
// Selected digest (always downloads PDFs) and single-article download
// ---------------------------------------------------------------------------

TEST_CASE("Background task: selected-digest export returns before the PDF downloads finish",
          "[background][digest]") {
    BlockedCore t;
    t.SelectBoth();

    bool started = false;
    auto elapsed = t.Time([&] { started = t.core->StartExportSelectedDigest(); });

    REQUIRE(started);
    REQUIRE(elapsed < 1s);
}

TEST_CASE("Background task: selected-digest export reports the digest directory when finished",
          "[background][digest]") {
    BlockedCore t;
    t.SelectBoth();
    REQUIRE(t.core->StartExportSelectedDigest());

    t.gate.Open();
    t.core->WaitForBackgroundTask();
    auto status = t.core->GetBackgroundTaskStatus();

    REQUIRE(status.done == 2);
    REQUIRE(fs::is_directory(status.result));
}

TEST_CASE("Background task: article download returns before the download finishes",
          "[background][download]") {
    BlockedCore t;

    bool started = false;
    auto elapsed = t.Time([&] { started = t.core->StartDownloadArticle(sample_articles[0].link); });

    REQUIRE(started);
    REQUIRE(elapsed < 1s);
}

TEST_CASE("Background task: article download completes without error", "[background][download]") {
    BlockedCore t;
    REQUIRE(t.core->StartDownloadArticle(sample_articles[0].link));

    t.gate.Open();
    t.core->WaitForBackgroundTask();
    auto status = t.core->GetBackgroundTaskStatus();

    REQUIRE(status.done == 1);
    REQUIRE(status.error.empty());
}

// ---------------------------------------------------------------------------
// TUI: the key handlers start the task and the screen shows its progress
// ---------------------------------------------------------------------------

namespace {

struct BlockedApp {
    DownloadGate gate;
    std::unique_ptr<Arxiv::ArxivApp> app;
    std::unique_ptr<trompeloeil::expectation> download;

    BlockedApp() {
        auto fetcher = std::make_unique<FetcherMock>();
        download = NAMED_ALLOW_CALL(*fetcher, DownloadPaper(ANY(std::string), ANY(std::string)))
                       .SIDE_EFFECT(gate.Wait())
                       .RETURN(true);
        Arxiv::KeyBindings kb{std::vector<Arxiv::Config::KeyMapping>{}};
        app = std::make_unique<Arxiv::ArxivApp>(
            task_config(), seeded_db(), std::move(fetcher), std::move(kb));
        app->GetCore().SetFilterIndex(Arxiv::AppCore::FilterView::All);
    }
    ~BlockedApp() {
        gate.Open();
        app.reset();
    }

    std::chrono::steady_clock::duration Press(const ftxui::Event& event) {
        auto t0 = std::chrono::steady_clock::now();
        app->GetEventHandler()->OnEvent(event);
        return std::chrono::steady_clock::now() - t0;
    }

    std::string Render() {
        auto screen =
            ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
        ftxui::Render(screen, app->GetEventHandler()->Render());
        return screen.ToString();
    }
};

} // namespace

TEST_CASE("TUI: Obsidian export key returns immediately and shows progress",
          "[background][tui][obsidian]") {
    BlockedApp t;
    t.app->GetCore().ToggleSelection(sample_articles[0].link);

    auto elapsed = t.Press(ftxui::Event::Character("o"));

    CHECK(elapsed < 1s);
    REQUIRE_THAT(t.Render(), ContainsSubstring("Exporting to Obsidian"));
}

TEST_CASE("TUI: download key returns immediately", "[background][tui][download]") {
    BlockedApp t;
    t.Press(ftxui::Event::Character("l")); // focus the article pane

    auto elapsed = t.Press(ftxui::Event::Character("d"));
    t.gate.Open();
    t.app->GetCore().WaitForBackgroundTask();

    CHECK(elapsed < 1s);
    REQUIRE(t.gate.calls == 1); // the key really reached the download handler
}
