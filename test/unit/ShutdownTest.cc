// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Quitting must not wait for the network. ~AppCore joins the fetch, refresh
// and task threads; a missed-week backfill (API pages paced 3 s apart, 15 s
// timeouts) or a PDF download (120 s timeout) used to hold the exit until it
// finished.

#include "Arxiv/AppCore.hh"
#include "Arxiv/Article.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/HttpClient.hh"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

// Every request hangs (as on a stalled connection) until Cancel, or 10 s.
struct HangingHttp : Arxiv::HttpClient {
    struct Shared {
        std::mutex mutex;
        std::condition_variable cv;
        bool cancelled = false;
        int in_flight = 0;
    };
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();

    Arxiv::HttpResponse Hang() {
        std::unique_lock<std::mutex> lock(shared->mutex);
        ++shared->in_flight;
        shared->cv.notify_all();
        shared->cv.wait_for(lock, 10s, [this] { return shared->cancelled; });
        return {0, "", "hung"};
    }
    Arxiv::HttpResponse Get(const std::string&, std::chrono::milliseconds) override {
        return Hang();
    }
    Arxiv::HttpResponse Download(const std::string&,
                                 std::chrono::milliseconds,
                                 const std::function<bool(std::string_view)>&) override {
        return Hang();
    }
    void Cancel() override {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->cancelled = true;
        shared->cv.notify_all();
    }
};

struct Setup {
    std::shared_ptr<HangingHttp::Shared> net;
    std::unique_ptr<Arxiv::AppCore> core;
};

Setup make_core(Arxiv::AppCore::FetchMode mode, const fs::path& downloads) {
    auto http = std::make_unique<HangingHttp>();
    auto net = http->shared;
    auto fetcher = std::make_unique<Arxiv::Fetcher>(
        std::vector<std::string>{"hep-ph"}, downloads.string(), std::move(http));
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    Arxiv::Article a;
    a.link = "https://arxiv.org/abs/2610.00001";
    a.title = "A paper";
    a.date = std::chrono::system_clock::now();
    db->AddArticle(a);
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir(downloads.string());
    cfg.set_ranker_file("/nonexistent/ranker.bin");
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db), std::move(fetcher), mode);
    return {net, std::move(core)};
}

void wait_for_request(HangingHttp::Shared& net) {
    std::unique_lock<std::mutex> lock(net.mutex);
    REQUIRE(net.cv.wait_for(lock, 5s, [&] { return net.in_flight > 0; }));
}

std::chrono::steady_clock::duration time_to_destroy(std::unique_ptr<Arxiv::AppCore>& core) {
    const auto start = std::chrono::steady_clock::now();
    core.reset();
    return std::chrono::steady_clock::now() - start;
}

} // namespace

TEST_CASE("Quitting during the start-up fetch does not wait for it", "[shutdown][regression]") {
    const fs::path dir = fs::temp_directory_path() / "arxiv_shutdown_fetch";
    auto setup = make_core(Arxiv::AppCore::FetchMode::Async, dir);
    wait_for_request(*setup.net);

    REQUIRE(time_to_destroy(setup.core) < 2s);
}

TEST_CASE("Quitting during a refresh does not wait for it", "[shutdown][regression]") {
    const fs::path dir = fs::temp_directory_path() / "arxiv_shutdown_refresh";
    auto setup = make_core(Arxiv::AppCore::FetchMode::Offline, dir);
    REQUIRE(setup.core->StartRefresh());
    wait_for_request(*setup.net);

    REQUIRE(time_to_destroy(setup.core) < 2s);
}

TEST_CASE("Quitting during a PDF download does not wait for it", "[shutdown][regression]") {
    const fs::path dir = fs::temp_directory_path() / "arxiv_shutdown_download";
    fs::create_directories(dir);
    auto setup = make_core(Arxiv::AppCore::FetchMode::Offline, dir);
    REQUIRE(setup.core->StartDownloadArticle("https://arxiv.org/abs/2610.00001"));
    wait_for_request(*setup.net);

    REQUIRE(time_to_destroy(setup.core) < 2s);
    fs::remove_all(dir);
}
