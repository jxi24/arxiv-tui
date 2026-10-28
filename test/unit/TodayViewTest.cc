// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// "Today" is the latest arXiv mailing, not the last 24 hours.
//
// RSS papers carry the mailing's pubDate (00:00 US Eastern). arXiv does not
// announce on Saturday or Sunday (or on holidays), so a rolling 24-hour window
// left Today, Recommended and the daily digests empty from Saturday morning
// until Monday's mailing arrived.

#include "Arxiv/AppCore.hh"
#include "Arxiv/Article.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "mocks/FetcherMock.hh"

using namespace std::chrono_literals;
using FetcherMock = arxiv_tui::test::FetcherMock;
namespace fs = std::filesystem;

namespace {

Arxiv::Article paper(const std::string& id, std::chrono::system_clock::time_point date) {
    Arxiv::Article a;
    a.link = "https://arxiv.org/abs/" + id;
    a.title = "Paper " + id;
    a.authors = "A. Author";
    a.abstract = "Abstract of " + id;
    a.date = date;
    return a;
}

std::unique_ptr<Arxiv::AppCore> offline_core(const std::vector<Arxiv::Article>& papers) {
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    for (const auto& a : papers)
        db->AddArticle(a);
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    cfg.set_ranker_file("/nonexistent/ranker.bin"); // untrained: no threshold
    return std::make_unique<Arxiv::AppCore>(
        cfg, std::move(db), std::make_unique<FetcherMock>(), Arxiv::AppCore::FetchMode::Offline);
}

std::vector<std::string> links(const std::vector<Arxiv::Article>& articles) {
    std::vector<std::string> out;
    for (const auto& a : articles)
        out.push_back(a.link);
    return out;
}

} // namespace

TEST_CASE("Today shows Friday's mailing over the weekend", "[today][regression]") {
    // Sunday afternoon: Friday's mailing is 60 h old, Thursday's 84 h.
    const auto now = std::chrono::system_clock::now();
    auto core = offline_core({paper("2610.00001", now - 60h), paper("2610.00002", now - 84h)});

    core->SetFilterIndex(Arxiv::AppCore::FilterView::Today);

    REQUIRE(links(core->GetCurrentArticles()) ==
            std::vector<std::string>{"https://arxiv.org/abs/2610.00001"});
}

TEST_CASE("Recommended ranks Friday's mailing over the weekend", "[today][regression]") {
    const auto now = std::chrono::system_clock::now();
    auto core = offline_core({paper("2610.00001", now - 60h), paper("2610.00002", now - 84h)});

    core->SetFilterIndex(Arxiv::AppCore::FilterView::Recommended);

    REQUIRE(links(core->GetCurrentArticles()) ==
            std::vector<std::string>{"https://arxiv.org/abs/2610.00001"});
}

TEST_CASE("Daily digests over the weekend hold Friday's mailing", "[today][digest][regression]") {
    const auto now = std::chrono::system_clock::now();
    auto core = offline_core({paper("2610.00001", now - 60h), paper("2610.00002", now - 84h)});
    const fs::path out = fs::temp_directory_path() / "today_view_digest.yaml";
    fs::remove(out);

    REQUIRE(core->ExportDailyDigestYAML(out.string()));
    std::ifstream in(out);
    std::stringstream content;
    content << in.rdbuf();
    fs::remove(out);

    CHECK_THAT(content.str(), Catch::Matchers::ContainsSubstring("2610.00001"));
    CHECK_THAT(content.str(), !Catch::Matchers::ContainsSubstring("2610.00002"));
}

TEST_CASE("Today leaves out the previous mailing, exactly 24 h earlier", "[today]") {
    // Weekday mailings are stamped 00:00 ET on consecutive days.
    const auto now = std::chrono::system_clock::now();
    const auto mailing = now - 2h;
    auto core = offline_core({paper("2610.00001", mailing), paper("2610.00002", mailing - 24h)});

    core->SetFilterIndex(Arxiv::AppCore::FilterView::Today);

    REQUIRE(links(core->GetCurrentArticles()) ==
            std::vector<std::string>{"https://arxiv.org/abs/2610.00001"});
}

TEST_CASE("DatabaseManager::GetLatestMailing ignores future-dated papers", "[today][database]") {
    // A malformed feed date in the future must not hide the real mailing.
    const auto now = std::chrono::system_clock::now();
    Arxiv::DatabaseManager db(":memory:");
    db.AddArticle(paper("2610.00001", now - 60h));
    db.AddArticle(paper("2610.00003", now + 24h * 30));

    REQUIRE(links(db.GetLatestMailing()) ==
            std::vector<std::string>{"https://arxiv.org/abs/2610.00001"});
}

TEST_CASE("DatabaseManager::GetLatestMailing on an empty database", "[today][database]") {
    Arxiv::DatabaseManager db(":memory:");
    REQUIRE(db.GetLatestMailing().empty());
}
