// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"
#include "Arxiv/Article.hh"
#include "Arxiv/Config.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fixtures/test_data.hh"
#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"

using namespace Catch::Matchers;
using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock = arxiv_tui::test::FetcherMock;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static std::unique_ptr<Arxiv::AppCore> make_core(DatabaseManagerMock*& db_out,
                                                 FetcherMock*& fetcher_out) {
    auto db_ptr = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    db_out = db_ptr.get();
    fetcher_out = fet_ptr.get();
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    return std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));
}

static std::string read_file(const fs::path& p) {
    std::ifstream f(p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------------------
// ExportDailyDigest — Markdown
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::ExportDailyDigest writes Markdown with article titles", "[digest]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "digest_test.md";
    fs::remove(tmp);

    auto articles = arxiv_tui::test::fixtures::sample_articles;
    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(articles);

    bool ok = core->ExportDailyDigest(tmp.string());
    REQUIRE(ok);
    REQUIRE(fs::exists(tmp));

    std::string content = read_file(tmp);
    REQUIRE_THAT(content, ContainsSubstring("Sample Article Title"));
    REQUIRE_THAT(content, ContainsSubstring("2403.12345"));

    fs::remove(tmp);
}

TEST_CASE("AppCore::ExportDailyDigest includes date header", "[digest]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "digest_header_test.md";
    fs::remove(tmp);

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(std::vector<Arxiv::Article>{});

    bool ok = core->ExportDailyDigest(tmp.string());
    REQUIRE(ok);

    std::string content = read_file(tmp);
    // Should have a date header (year 20xx)
    REQUIRE_THAT(content, ContainsSubstring("20"));

    fs::remove(tmp);
}

TEST_CASE("AppCore::ExportDailyDigest with empty articles writes only header", "[digest]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "digest_empty_test.md";
    fs::remove(tmp);

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(std::vector<Arxiv::Article>{});

    bool ok = core->ExportDailyDigest(tmp.string());
    REQUIRE(ok);
    // File should exist and not be completely empty
    REQUIRE(fs::file_size(tmp) > 0);

    fs::remove(tmp);
}

TEST_CASE("AppCore::ExportDailyDigest returns false for unwritable path", "[digest]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(std::vector<Arxiv::Article>{});

    bool ok = core->ExportDailyDigest("/no_such_dir/digest.md");
    REQUIRE_FALSE(ok);
}

// ---------------------------------------------------------------------------
// ExportDailyDigestYAML
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::ExportDailyDigestYAML writes YAML with article data", "[digest]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "digest_test.yaml";
    fs::remove(tmp);

    auto articles = arxiv_tui::test::fixtures::sample_articles;
    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(articles);

    bool ok = core->ExportDailyDigestYAML(tmp.string());
    REQUIRE(ok);
    REQUIRE(fs::exists(tmp));

    std::string content = read_file(tmp);
    REQUIRE_THAT(content, ContainsSubstring("title:"));
    REQUIRE_THAT(content, ContainsSubstring("Sample Article Title"));
    REQUIRE_THAT(content, ContainsSubstring("link:"));

    fs::remove(tmp);
}

TEST_CASE("AppCore::ExportDailyDigestYAML returns false for unwritable path", "[digest]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(std::vector<Arxiv::Article>{});

    bool ok = core->ExportDailyDigestYAML("/no_such_dir/digest.yaml");
    REQUIRE_FALSE(ok);
}

TEST_CASE("AppCore::ExportDailyDigestYAML escapes quotes in article fields", "[digest]") {
    // Exercises the escape_yaml lambda's '"' branch (line 1252).
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "digest_escape_test.yaml";
    fs::remove(tmp);

    Arxiv::Article special = arxiv_tui::test::fixtures::sample_articles[0];
    special.title = "A \"quoted\" title";
    special.authors = "O'Brien, \"Alias\"";
    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(std::vector<Arxiv::Article>{special});

    bool ok = core->ExportDailyDigestYAML(tmp.string());
    REQUIRE(ok);

    std::string content = read_file(tmp);
    REQUIRE_THAT(content, ContainsSubstring("\\\"quoted\\\""));

    fs::remove(tmp);
}
