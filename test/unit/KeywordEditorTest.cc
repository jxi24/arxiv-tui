// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"

using namespace Catch::Matchers;
using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock = arxiv_tui::test::FetcherMock;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static std::unique_ptr<Arxiv::AppCore> make_core_with_kw_file(const std::string& kw_path,
                                                              DatabaseManagerMock*& db_out,
                                                              FetcherMock*& fetcher_out) {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    db_out = db_ptr.get();
    fetcher_out = fet_ptr.get();
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    cfg.set_keywords_file(kw_path);
    return std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));
}

// ---------------------------------------------------------------------------
// AppCore::ReloadKeywords
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::ReloadKeywords: GetKeywords returns empty when no file set",
          "[keyword][editor][appcore]") {
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;

    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    db = db_ptr.get();
    fetcher = fet_ptr.get();
    (void)fetcher;
    (void)db;

    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    // no keywords_file set
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));

    REQUIRE(core->GetKeywords().empty());
}

TEST_CASE("AppCore::ReloadKeywords: loads keywords from file", "[keyword][editor][appcore]") {
    fs::path tmp = fs::temp_directory_path() / "test_keywords.txt";
    {
        std::ofstream f(tmp);
        f << "quantum\n";
        f << "gravity\n";
        f << "neural networks\n";
        f << "\n"; // blank line — should be ignored
    }

    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core_with_kw_file(tmp.string(), db, fetcher);
    core->ReloadKeywords();

    auto kws = core->GetKeywords();
    REQUIRE(kws.size() == 3);
    REQUIRE(std::find(kws.begin(), kws.end(), "quantum") != kws.end());
    REQUIRE(std::find(kws.begin(), kws.end(), "gravity") != kws.end());
    REQUIRE(std::find(kws.begin(), kws.end(), "neural networks") != kws.end());

    fs::remove(tmp);
}

TEST_CASE("AppCore::ReloadKeywords: silently ignores nonexistent file",
          "[keyword][editor][appcore]") {
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core_with_kw_file("/nonexistent/path/keywords.txt", db, fetcher);

    REQUIRE_NOTHROW(core->ReloadKeywords());
    REQUIRE(core->GetKeywords().empty());
}

// ---------------------------------------------------------------------------
// AppCore::SaveKeywords
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::SaveKeywords: writes keywords to file", "[keyword][editor][appcore]") {
    fs::path tmp = fs::temp_directory_path() / "test_save_keywords.txt";
    fs::remove(tmp);

    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core_with_kw_file(tmp.string(), db, fetcher);

    core->SaveKeywords({"machine learning", "transformer", "attention"});
    REQUIRE(fs::exists(tmp));

    std::ifstream f(tmp);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(f, line))
        if (!line.empty())
            lines.push_back(line);

    REQUIRE(lines.size() == 3);
    REQUIRE(std::find(lines.begin(), lines.end(), "machine learning") != lines.end());
    REQUIRE(std::find(lines.begin(), lines.end(), "transformer") != lines.end());

    fs::remove(tmp);
}

TEST_CASE("AppCore::SaveKeywords: updates GetKeywords immediately", "[keyword][editor][appcore]") {
    fs::path tmp = fs::temp_directory_path() / "test_update_keywords.txt";
    fs::remove(tmp);

    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core_with_kw_file(tmp.string(), db, fetcher);

    core->SaveKeywords({"diffusion", "generative"});
    auto kws = core->GetKeywords();
    REQUIRE(std::find(kws.begin(), kws.end(), "diffusion") != kws.end());
    REQUIRE(std::find(kws.begin(), kws.end(), "generative") != kws.end());

    fs::remove(tmp);
}

TEST_CASE("AppCore::SaveKeywords: returns false when path is unwritable",
          "[keyword][editor][appcore]") {
    DatabaseManagerMock* db = nullptr;
    FetcherMock* fetcher = nullptr;
    auto core = make_core_with_kw_file("/no_such_dir/kw.txt", db, fetcher);

    bool ok = core->SaveKeywords({"word"});
    REQUIRE_FALSE(ok);
}
