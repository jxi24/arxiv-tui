// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"
#include "Arxiv/Article.hh"
#include "Arxiv/Config.hh"

#include <yaml-cpp/yaml.h>

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

TEST_CASE("AppCore::ExportDailyDigestYAML includes arXiv id, category and score", "[digest]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "digest_fields_test.yaml";
    fs::remove(tmp);
    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(arxiv_tui::test::fixtures::sample_articles);

    REQUIRE(core->ExportDailyDigestYAML(tmp.string()));
    std::string content = read_file(tmp);
    REQUIRE_THAT(content, ContainsSubstring("id: \"2403.12345\""));
    REQUIRE_THAT(content, ContainsSubstring("category: \"cs.AI\""));
    REQUIRE_THAT(content, ContainsSubstring("score: "));

    fs::remove(tmp);
}

TEST_CASE("AppCore::ExportDailyDigestYAML lists articles by predicted score, highest first",
          "[digest]") {
    fs::path keywords = fs::temp_directory_path() / "digest_keywords.txt";
    {
        std::ofstream kf(keywords);
        kf << "another\n";
    }
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    auto* db_ptr = db.get();
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    cfg.set_keywords_file(keywords.string());
    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(arxiv_tui::test::fixtures::sample_articles);
    auto core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db), std::move(fetcher));
    core->ReloadKeywords();

    fs::path tmp = fs::temp_directory_path() / "digest_ranked_test.yaml";
    fs::remove(tmp);
    REQUIRE(core->ExportDailyDigestYAML(tmp.string()));
    std::string content = read_file(tmp);
    // "Another Test Article" matches the keyword, so it comes first despite the DB order
    auto matched = content.find("Another Test Article");
    auto other = content.find("Sample Article Title");
    REQUIRE(matched != std::string::npos);
    REQUIRE(other != std::string::npos);
    REQUIRE(matched < other);

    fs::remove(tmp);
    fs::remove(keywords);
}

// ---------------------------------------------------------------------------
// ExportSelectedDigestArchive
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::ExportSelectedDigestArchive returns empty when nothing selected",
          "[digest][archive]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(arxiv_tui::test::fixtures::sample_articles);

    REQUIRE(core->ExportSelectedDigestArchive().empty());
}

TEST_CASE("AppCore::ExportSelectedDigestArchive produces a .tar.gz archive", "[digest][archive]") {
    fs::path tmp_dir = fs::temp_directory_path() / "arxiv_archive_test";
    fs::remove_all(tmp_dir);

    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir(tmp_dir.string());

    auto db_ptr_owned = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr_owned = std::make_unique<FetcherMock>();
    auto* db_ptr = db_ptr_owned.get();
    auto* fet_ptr = fet_ptr_owned.get();

    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(arxiv_tui::test::fixtures::sample_articles);
    ALLOW_CALL(*db_ptr, GetProjects()).RETURN(std::vector<std::string>{});
    ALLOW_CALL(*fet_ptr, DownloadPaper(ANY(std::string), ANY(std::string))).RETURN(false);

    Arxiv::AppCore core(cfg, std::move(db_ptr_owned), std::move(fet_ptr_owned));
    core.ToggleSelection(arxiv_tui::test::fixtures::sample_articles[0].link);

    auto archive_path = core.ExportSelectedDigestArchive();

    REQUIRE_FALSE(archive_path.empty());
    REQUIRE(fs::exists(archive_path));
    REQUIRE_THAT(archive_path, EndsWith(".tar.gz"));

    fs::remove_all(tmp_dir);
    fs::remove(archive_path);
}

TEST_CASE("AppCore::ExportDailyDigestYAML leaves out replacements (new versions of older papers)",
          "[digest]") {
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    Arxiv::Article fresh;
    fresh.link = "https://arxiv.org/abs/2610.00001";
    fresh.title = "First posted today";
    Arxiv::Article replaced;
    replaced.link = "https://arxiv.org/abs/2301.00002";
    replaced.title = "Old paper, new version";
    replaced.is_replacement = true;

    fs::path tmp = fs::temp_directory_path() / "digest_no_replacements_test.yaml";
    fs::remove(tmp);
    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(std::vector<Arxiv::Article>{replaced, fresh});

    REQUIRE(core->ExportDailyDigestYAML(tmp.string()));
    std::string content = read_file(tmp);
    SECTION("the new paper is listed") {
        REQUIRE_THAT(content, ContainsSubstring("First posted today"));
    }
    SECTION("the replacement is not") {
        REQUIRE_THAT(content, !ContainsSubstring("Old paper, new version"));
    }

    fs::remove(tmp);
}

// ---------------------------------------------------------------------------
// Regressions found in code review — each fails until the bug is fixed.
// ---------------------------------------------------------------------------

TEST_CASE("Digest regression: YAML digest stays valid when fields contain LaTeX backslashes",
          "[digest][regression]") {
    // In a double-quoted YAML scalar "\m" is an invalid escape and "\t" is a
    // tab, so arXiv titles like the one below broke the file.
    DatabaseManagerMock* db_ptr = nullptr;
    FetcherMock* fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    Arxiv::Article article = arxiv_tui::test::fixtures::sample_articles[0];
    article.title = R"(Bounds on $\mu\to e\gamma$)";
    article.abstract = "Line one\nwith \\textbf{bold} and a tab\there";
    ALLOW_CALL(*db_ptr, GetRecent(ANY(int))).RETURN(std::vector<Arxiv::Article>{article});

    fs::path tmp = fs::temp_directory_path() / "digest_regression_latex.yaml";
    fs::remove(tmp);
    REQUIRE(core->ExportDailyDigestYAML(tmp.string()));

    YAML::Node doc;
    REQUIRE_NOTHROW(doc = YAML::LoadFile(tmp.string()));
    REQUIRE(doc["articles"][0]["title"].as<std::string>() == article.title);
    REQUIRE(doc["articles"][0]["abstract"].as<std::string>() == article.abstract);
    fs::remove(tmp);
}
