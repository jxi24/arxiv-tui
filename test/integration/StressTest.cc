// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// ---------------------------------------------------------------------------
// Integration stress tests
//
// These tests simulate realistic multi-step user sessions by building JSONL
// replay scripts and dispatching them through ReplayPlayer into an AppCore
// backed by a *real* in-memory SQLite database. The Fetcher is mocked because
// we cannot hit the network, but every other layer is live.
//
// The goal is to verify that typical (and atypical) interaction sequences
// complete without crashes, exceptions, or data corruption.
// ---------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "Arxiv/AppCore.hh"
#include "Arxiv/Article.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Replay.hh"

#include "mocks/FetcherMock.hh"

using namespace Catch::Matchers;
using FetcherMock = arxiv_tui::test::FetcherMock;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Generate a batch of distinct articles.
static std::vector<Arxiv::Article> make_articles(int count) {
    std::vector<Arxiv::Article> articles;
    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < count; ++i) {
        Arxiv::Article a;
        a.title    = "Stress Article " + std::to_string(i);
        a.link     = "https://arxiv.org/abs/2400." + std::to_string(10000 + i);
        a.abstract = "Abstract text for article number " + std::to_string(i) +
                     ". Keywords: quantum, lattice, boson, fermion, symmetry.";
        a.authors  = "Author A" + std::to_string(i) + ", Author B" + std::to_string(i);
        a.date     = now - std::chrono::hours(i);
        a.category = "hep-ph";
        a.bookmarked = false;
        articles.push_back(std::move(a));
    }
    return articles;
}

/// Build a minimal AppCore with a real in-memory DB and a mock Fetcher seeded
/// with `articles`.  The raw Fetcher pointer is returned so callers can add
/// more mock expectations if needed.
struct CoreBundle {
    std::unique_ptr<Arxiv::AppCore> core;
    FetcherMock* fetcher;           // non-owning
    fs::path     tmp_dir;           // per-test temp directory for export files
};

static CoreBundle make_core(const std::vector<Arxiv::Article>& articles) {
    CoreBundle b;
    // Per-pid tmp dir so ctest -j N doesn't have parallel processes step on
    // each other's exported files.
    b.tmp_dir = fs::temp_directory_path() /
                ("arxiv_stress_test_" + std::to_string(::getpid()));
    fs::create_directories(b.tmp_dir);

    auto db  = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    auto fet = std::make_unique<FetcherMock>();
    b.fetcher = fet.get();

    fet->setFetchResponse(articles);
    // BibTeX fetch returns empty (fallback path in AppCore will construct one)
    fet->setBibTeXResponse("", "");
    // DownloadPaper always succeeds
    fet->setDownloadPaperResponse(true);

    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir(b.tmp_dir.string());
    cfg.set_retrain_interval(3);
    cfg.set_recommend_threshold(2.0f);

    b.core = std::make_unique<Arxiv::AppCore>(cfg, std::move(db), std::move(fet));
    return b;
}

/// Convenience: build a single JSONL line.
static std::string jline(const std::string& json_body) {
    return json_body + "\n";
}

// ---------------------------------------------------------------------------
// Scenario 1: Browse articles, toggle bookmarks, switch filters
// ---------------------------------------------------------------------------
TEST_CASE("Stress: browsing and bookmarking session", "[integration][stress]") {
    auto articles = make_articles(20);
    auto [core, fetcher, tmp] = make_core(articles);

    // Build a replay that mimics a user scrolling through articles, bookmarking
    // several, switching between All and Bookmarks filters repeatedly.
    std::string script;
    for (int i = 0; i < 20; ++i) {
        script += jline(R"({"ts":)" + std::to_string(1000 + i) +
                        R"(,"action":"set_article_index","index":)" +
                        std::to_string(i) + "}");
    }
    // Bookmark articles 0, 5, 10, 15
    for (int i : {0, 5, 10, 15}) {
        script += jline(R"({"ts":2000,"action":"toggle_bookmark","article_link":")" +
                        articles[static_cast<std::size_t>(i)].link + R"("})");
    }
    // Switch to Bookmarks filter (index 1) then back to All (0) several times
    for (int round = 0; round < 5; ++round) {
        script += jline(R"({"ts":3000,"action":"set_filter_index","index":1})");
        script += jline(R"({"ts":3001,"action":"set_filter_index","index":0})");
    }
    // Un-bookmark article 5
    script += jline(R"({"ts":4000,"action":"toggle_bookmark","article_link":")" +
                    articles[5].link + R"("})");

    auto result = Arxiv::ReplayPlayer::FromString(script, *core);
    REQUIRE(result.error.empty());
    REQUIRE(result.replayed > 0);
    // State is consistent — we can still query articles
    REQUIRE_FALSE(core->GetCurrentArticles().empty());
}

// ---------------------------------------------------------------------------
// Scenario 2: Project lifecycle — create, populate, nest, annotate, export, delete
// ---------------------------------------------------------------------------
TEST_CASE("Stress: full project lifecycle", "[integration][stress]") {
    auto articles = make_articles(10);
    auto [core, fetcher, tmp] = make_core(articles);

    std::string md_path   = (tmp / "proj.md").string();
    std::string txt_path  = (tmp / "proj.txt").string();
    std::string json_path = (tmp / "proj.json").string();
    std::string bib_path  = (tmp / "proj.bib").string();

    std::string script;

    // Create projects
    script += jline(R"({"ts":1000,"action":"add_project","name":"Physics"})");
    script += jline(R"({"ts":1001,"action":"add_project","name":"Quantum"})");
    script += jline(R"({"ts":1002,"action":"add_project","name":"Lattice"})");

    // Set hierarchy: Quantum and Lattice are children of Physics
    script += jline(R"({"ts":1003,"action":"set_project_parent","project":"Quantum","parent":"Physics"})");
    script += jline(R"({"ts":1004,"action":"set_project_parent","project":"Lattice","parent":"Physics"})");

    // Link articles to projects
    for (int i = 0; i < 5; ++i) {
        script += jline(R"({"ts":)" + std::to_string(2000 + i) +
                        R"(,"action":"link_article_to_project","article_link":")" +
                        articles[static_cast<std::size_t>(i)].link + R"(","project":"Quantum"})");
    }
    for (int i = 5; i < 10; ++i) {
        script += jline(R"({"ts":)" + std::to_string(2000 + i) +
                        R"(,"action":"link_article_to_project","article_link":")" +
                        articles[static_cast<std::size_t>(i)].link + R"(","project":"Lattice"})");
    }

    // Annotate a few articles
    script += jline(R"({"ts":3000,"action":"set_project_note","project":"Quantum","article_link":")" +
                    articles[0].link + R"(","note":"Key result on entanglement"})");
    script += jline(R"({"ts":3001,"action":"set_project_note","project":"Lattice","article_link":")" +
                    articles[5].link + R"(","note":"Good lattice spacing analysis"})");

    // Browse into Quantum project filter (index 6 = first project after the 6 built-in filters)
    script += jline(R"({"ts":4000,"action":"set_filter_index","index":6})");
    script += jline(R"({"ts":4001,"action":"set_article_index","index":0})");

    // Export project in all formats
    script += jline(R"({"ts":5000,"action":"export_project_markdown","project":"Quantum","path":")" + md_path + R"("})");
    script += jline(R"({"ts":5001,"action":"export_project_text","project":"Quantum","path":")" + txt_path + R"("})");
    script += jline(R"({"ts":5002,"action":"export_project_json","project":"Quantum","path":")" + json_path + R"("})");
    script += jline(R"({"ts":5003,"action":"export_project_bibtex","project":"Quantum","path":")" + bib_path + R"("})");

    // Unlink an article
    script += jline(R"({"ts":6000,"action":"unlink_article_from_project","article_link":")" +
                    articles[0].link + R"(","project":"Quantum"})");

    // Delete a child project
    script += jline(R"({"ts":7000,"action":"remove_project","name":"Lattice"})");

    // Switch back to All
    script += jline(R"({"ts":8000,"action":"set_filter_index","index":0})");

    auto result = Arxiv::ReplayPlayer::FromString(script, *core);
    REQUIRE(result.error.empty());
    REQUIRE(result.replayed > 0);

    // Exported files should exist
    REQUIRE(fs::exists(md_path));
    REQUIRE(fs::exists(txt_path));
    REQUIRE(fs::exists(json_path));
    REQUIRE(fs::exists(bib_path));

    // Verify exported BibTeX is non-empty
    {
        std::ifstream f(bib_path);
        std::stringstream ss;
        ss << f.rdbuf();
        REQUIRE_FALSE(ss.str().empty());
    }

    // Clean up
    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Scenario 3: Rating articles and training the ranker
// ---------------------------------------------------------------------------
TEST_CASE("Stress: rating and ranking workflow", "[integration][stress]") {
    auto articles = make_articles(15);
    auto [core, fetcher, tmp] = make_core(articles);

    std::string script;

    // Rate enough articles to trigger auto-retrain (threshold = 3)
    for (int i = 0; i < 10; ++i) {
        int rating = (i % 5) + 1; // ratings 1-5 cycling
        script += jline(R"({"ts":)" + std::to_string(1000 + i) +
                        R"(,"action":"rate_article","article_link":")" +
                        articles[static_cast<std::size_t>(i)].link +
                        R"(","rating":)" + std::to_string(rating) + "}");
    }

    // Force retrain
    script += jline(R"({"ts":5000,"action":"force_retrain"})");

    // Switch to Recommended filter (index 5)
    script += jline(R"({"ts":6000,"action":"set_filter_index","index":5})");
    script += jline(R"({"ts":6001,"action":"set_article_index","index":0})");

    // Switch back to All
    script += jline(R"({"ts":7000,"action":"set_filter_index","index":0})");

    auto result = Arxiv::ReplayPlayer::FromString(script, *core);
    REQUIRE(result.error.empty());
    REQUIRE(result.replayed > 0);

    // Wait for any background training to complete before querying
    core->TryRefetchIfNeeded();

    // Ranker should be trained now
    REQUIRE(core->IsRankerTrained());

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Scenario 4: Search and date range filtering
// ---------------------------------------------------------------------------
TEST_CASE("Stress: search and date-range filtering", "[integration][stress]") {
    auto articles = make_articles(20);
    auto [core, fetcher, tmp] = make_core(articles);

    std::string script;

    // Set date range filter
    script += jline(R"({"ts":1000,"action":"set_date_range","start":"2024-01-01","end":"2026-12-31"})");
    script += jline(R"({"ts":1001,"action":"set_filter_index","index":3})"); // Range filter
    script += jline(R"({"ts":1002,"action":"set_article_index","index":0})");

    // Search across all fields
    script += jline(R"({"ts":2000,"action":"set_search_query","query":"quantum","title":true,"authors":false,"abstract_field":true})");
    script += jline(R"({"ts":2001,"action":"set_filter_index","index":4})"); // Search filter
    script += jline(R"({"ts":2002,"action":"set_article_index","index":0})");

    // Search with only title
    script += jline(R"({"ts":3000,"action":"set_search_query","query":"Stress","title":true,"authors":false,"abstract_field":false})");
    script += jline(R"({"ts":3001,"action":"set_filter_index","index":4})");

    // Search with empty query (edge case)
    script += jline(R"({"ts":4000,"action":"set_search_query","query":"","title":true,"authors":true,"abstract_field":true})");
    script += jline(R"({"ts":4001,"action":"set_filter_index","index":4})");

    // Switch back to All
    script += jline(R"({"ts":5000,"action":"set_filter_index","index":0})");

    auto result = Arxiv::ReplayPlayer::FromString(script, *core);
    REQUIRE(result.error.empty());
    REQUIRE(result.replayed > 0);

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Scenario 5: Rapid filter switching (fuzz-like)
// ---------------------------------------------------------------------------
TEST_CASE("Stress: rapid filter and index cycling", "[integration][stress]") {
    auto articles = make_articles(30);
    auto [core, fetcher, tmp] = make_core(articles);

    // Add a project so filter index 6 is valid
    std::string script;
    script += jline(R"({"ts":100,"action":"add_project","name":"TestProj"})");

    // Rapidly cycle through every filter 50 times
    int ts = 1000;
    for (int round = 0; round < 50; ++round) {
        for (int filter = 0; filter <= 6; ++filter) {
            script += jline(R"({"ts":)" + std::to_string(ts++) +
                            R"(,"action":"set_filter_index","index":)" +
                            std::to_string(filter) + "}");
        }
    }

    // Rapidly cycle article index within bounds and out of bounds
    for (int i = -5; i < 35; ++i) {
        script += jline(R"({"ts":)" + std::to_string(ts++) +
                        R"(,"action":"set_article_index","index":)" +
                        std::to_string(i) + "}");
    }

    auto result = Arxiv::ReplayPlayer::FromString(script, *core);
    REQUIRE(result.error.empty());
    REQUIRE(result.replayed > 0);

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Scenario 6: Record and replay round-trip
// ---------------------------------------------------------------------------
TEST_CASE("Stress: record actions then replay into a fresh core", "[integration][stress]") {
    auto articles = make_articles(10);

    // Phase 1: record actions via ReplayRecorder
    Arxiv::ReplayRecorder recorder;
    recorder.RecordSetFilterIndex(0);
    recorder.RecordSetArticleIndex(3);
    recorder.RecordToggleBookmark(articles[3].link);
    recorder.RecordAddProject("Replay-Test");
    recorder.RecordLinkArticleToProject(articles[0].link, "Replay-Test");
    recorder.RecordLinkArticleToProject(articles[1].link, "Replay-Test");
    recorder.RecordSetProjectNote("Replay-Test", articles[0].link, "Important paper");
    recorder.RecordSetProjectParent("Replay-Test", "");
    recorder.RecordSetSearchQuery("Article", true, true, true);
    recorder.RecordSetFilterIndex(4); // Search filter
    recorder.RecordSetDateRange("2024-01-01", "2026-12-31");
    recorder.RecordSetFilterIndex(3); // Range filter
    recorder.RecordRateArticle(articles[2].link, 5);
    recorder.RecordRateArticle(articles[4].link, 2);
    recorder.RecordSetFilterIndex(0); // Back to All

    std::string jsonl = recorder.GetJSONL();
    REQUIRE(recorder.GetCount() == 15);

    // Phase 2: replay into a fresh AppCore
    auto [core, fetcher, tmp] = make_core(articles);
    auto result = Arxiv::ReplayPlayer::FromString(jsonl, *core);
    REQUIRE(result.error.empty());
    REQUIRE(result.replayed == 15);
    REQUIRE(result.skipped  == 0);

    // Verify state survived replay
    REQUIRE(core->GetFilterIndex() == 0);

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Scenario 7: Project JSON export then re-import
// ---------------------------------------------------------------------------
TEST_CASE("Stress: export project as JSON then import into fresh core", "[integration][stress]") {
    auto articles = make_articles(5);
    auto [core1, fetcher1, tmp] = make_core(articles);

    std::string json_path = (tmp / "roundtrip.json").string();

    // Build a project with notes and export it
    std::string setup;
    setup += jline(R"({"ts":1000,"action":"add_project","name":"RoundTrip"})");
    for (int i = 0; i < 5; ++i) {
        setup += jline(R"({"ts":)" + std::to_string(1001 + i) +
                       R"(,"action":"link_article_to_project","article_link":")" +
                       articles[static_cast<std::size_t>(i)].link + R"(","project":"RoundTrip"})");
    }
    setup += jline(R"({"ts":2000,"action":"set_project_note","project":"RoundTrip","article_link":")" +
                   articles[0].link + R"(","note":"First note"})");
    setup += jline(R"({"ts":3000,"action":"export_project_json","project":"RoundTrip","path":")" +
                   json_path + R"("})");

    auto r1 = Arxiv::ReplayPlayer::FromString(setup, *core1);
    REQUIRE(r1.error.empty());
    REQUIRE(fs::exists(json_path));

    // Import into a second fresh core
    auto [core2, fetcher2, tmp2] = make_core(articles);
    std::string import_script;
    import_script += jline(R"({"ts":4000,"action":"import_project_json","path":")" + json_path + R"("})");

    auto r2 = Arxiv::ReplayPlayer::FromString(import_script, *core2);
    REQUIRE(r2.error.empty());
    REQUIRE(r2.replayed == 1);

    // The imported project should now exist
    auto projects = core2->GetProjects();
    REQUIRE(std::find(projects.begin(), projects.end(), "RoundTrip") != projects.end());

    fs::remove_all(tmp);
    fs::remove_all(tmp2);
}

// ---------------------------------------------------------------------------
// Scenario 8: BibTeX export for articles
// ---------------------------------------------------------------------------
TEST_CASE("Stress: BibTeX export for single and project articles", "[integration][stress]") {
    auto articles = make_articles(5);
    auto [core, fetcher, tmp] = make_core(articles);

    std::string single_bib  = (tmp / "single.bib").string();
    std::string project_bib = (tmp / "project.bib").string();

    std::string script;
    // Export a single article BibTeX
    script += jline(R"({"ts":1000,"action":"export_article_bibtex","article_link":")" +
                    articles[0].link + R"(","path":")" + single_bib + R"("})");

    // Create project, link articles, export project BibTeX
    script += jline(R"({"ts":2000,"action":"add_project","name":"BibProject"})");
    for (int i = 0; i < 3; ++i) {
        script += jline(R"({"ts":)" + std::to_string(2001 + i) +
                        R"(,"action":"link_article_to_project","article_link":")" +
                        articles[static_cast<std::size_t>(i)].link + R"(","project":"BibProject"})");
    }
    script += jline(R"({"ts":3000,"action":"export_project_bibtex","project":"BibProject","path":")" +
                    project_bib + R"("})");

    auto result = Arxiv::ReplayPlayer::FromString(script, *core);
    REQUIRE(result.error.empty());

    REQUIRE(fs::exists(single_bib));
    REQUIRE(fs::exists(project_bib));

    // Verify BibTeX content has @article entries
    {
        std::ifstream f(project_bib);
        std::stringstream ss;
        ss << f.rdbuf();
        REQUIRE_THAT(ss.str(), ContainsSubstring("@article"));
    }

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Scenario 9: Edge cases — operations on empty state and invalid references
// ---------------------------------------------------------------------------
TEST_CASE("Stress: edge cases do not crash", "[integration][stress]") {
    auto articles = make_articles(5);
    auto [core, fetcher, tmp] = make_core(articles);

    std::string script;

    // Toggle bookmark on non-existent article link
    script += jline(R"({"ts":1000,"action":"toggle_bookmark","article_link":"https://arxiv.org/abs/9999.99999"})");

    // Link to non-existent project (AddProject was never called)
    script += jline(R"({"ts":1001,"action":"link_article_to_project","article_link":")" +
                    articles[0].link + R"(","project":"NonExistent"})");

    // Unlink from non-existent project
    script += jline(R"({"ts":1002,"action":"unlink_article_from_project","article_link":")" +
                    articles[0].link + R"(","project":"NonExistent"})");

    // Remove non-existent project
    script += jline(R"({"ts":1003,"action":"remove_project","name":"Ghost"})");

    // Set parent on non-existent project
    script += jline(R"({"ts":1004,"action":"set_project_parent","project":"Ghost","parent":"AlsoGhost"})");

    // Set note on non-existent project
    script += jline(R"({"ts":1005,"action":"set_project_note","project":"Ghost","article_link":")" +
                    articles[0].link + R"(","note":"orphan note"})");

    // Rate with out-of-range values
    script += jline(R"({"ts":1006,"action":"rate_article","article_link":")" +
                    articles[0].link + R"(","rating":0})");
    script += jline(R"({"ts":1007,"action":"rate_article","article_link":")" +
                    articles[0].link + R"(","rating":100})");

    // Set filter to out-of-range index
    script += jline(R"({"ts":1008,"action":"set_filter_index","index":999})");
    script += jline(R"({"ts":1009,"action":"set_filter_index","index":-1})");

    // Set article index to negative
    script += jline(R"({"ts":1010,"action":"set_article_index","index":-10})");

    // Export from non-existent project
    std::string dead_path = (tmp / "dead.md").string();
    script += jline(R"({"ts":1011,"action":"export_project_markdown","project":"NoSuchProject","path":")" +
                    dead_path + R"("})");
    script += jline(R"({"ts":1012,"action":"export_project_bibtex","project":"NoSuchProject","path":")" +
                    dead_path + R"("})");

    // Import from non-existent file
    script += jline(R"({"ts":1013,"action":"import_project_json","path":"/tmp/no_such_file_xyz.json"})");

    // Search with special characters
    script += jline(R"({"ts":1014,"action":"set_search_query","query":"O'Brien \"quoted\" (parens) [brackets]","title":true,"authors":true,"abstract_field":true})");
    script += jline(R"({"ts":1015,"action":"set_filter_index","index":4})");

    // Return to safe state
    script += jline(R"({"ts":9999,"action":"set_filter_index","index":0})");

    auto result = Arxiv::ReplayPlayer::FromString(script, *core);
    // We expect all actions to replay (no parse errors), even if some are no-ops
    REQUIRE(result.error.empty());
    REQUIRE(result.replayed > 0);

    // Core is still usable
    REQUIRE_FALSE(core->GetCurrentArticles().empty());

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Scenario 10: Full session — combines all feature areas
// ---------------------------------------------------------------------------
TEST_CASE("Stress: full realistic session combining all features", "[integration][stress]") {
    auto articles = make_articles(25);
    auto [core, fetcher, tmp] = make_core(articles);

    std::string bib_path = (tmp / "session.bib").string();
    std::string md_path  = (tmp / "session.md").string();

    std::string script;
    int ts = 1000;

    // --- Phase 1: Browse and bookmark ---
    for (int i = 0; i < 10; ++i) {
        script += jline(R"({"ts":)" + std::to_string(ts++) +
                        R"(,"action":"set_article_index","index":)" + std::to_string(i) + "}");
    }
    for (int i : {1, 3, 7}) {
        script += jline(R"({"ts":)" + std::to_string(ts++) +
                        R"(,"action":"toggle_bookmark","article_link":")" +
                        articles[static_cast<std::size_t>(i)].link + R"("})");
    }
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"set_filter_index","index":1})"); // Bookmarks
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"set_filter_index","index":0})"); // All

    // --- Phase 2: Create projects and organize ---
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"add_project","name":"Session-Root"})");
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"add_project","name":"Session-Child"})");
    script += jline(R"({"ts":)" + std::to_string(ts++) +
                    R"(,"action":"set_project_parent","project":"Session-Child","parent":"Session-Root"})");

    for (int i = 0; i < 8; ++i) {
        script += jline(R"({"ts":)" + std::to_string(ts++) +
                        R"(,"action":"link_article_to_project","article_link":")" +
                        articles[static_cast<std::size_t>(i)].link + R"(","project":"Session-Root"})");
    }
    for (int i = 3; i < 6; ++i) {
        script += jline(R"({"ts":)" + std::to_string(ts++) +
                        R"(,"action":"link_article_to_project","article_link":")" +
                        articles[static_cast<std::size_t>(i)].link + R"(","project":"Session-Child"})");
    }

    // Add notes
    script += jline(R"({"ts":)" + std::to_string(ts++) +
                    R"(,"action":"set_project_note","project":"Session-Root","article_link":")" +
                    articles[0].link + R"(","note":"Lead paper for the session"})");

    // --- Phase 3: Rate articles ---
    for (int i = 0; i < 8; ++i) {
        int rating = (i % 5) + 1;
        script += jline(R"({"ts":)" + std::to_string(ts++) +
                        R"(,"action":"rate_article","article_link":")" +
                        articles[static_cast<std::size_t>(i)].link +
                        R"(","rating":)" + std::to_string(rating) + "}");
    }
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"force_retrain"})");

    // --- Phase 4: Search and date range ---
    script += jline(R"({"ts":)" + std::to_string(ts++) +
                    R"(,"action":"set_search_query","query":"Article","title":true,"authors":false,"abstract_field":true})");
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"set_filter_index","index":4})");
    script += jline(R"({"ts":)" + std::to_string(ts++) +
                    R"(,"action":"set_date_range","start":"2024-01-01","end":"2026-12-31"})");
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"set_filter_index","index":3})");

    // --- Phase 5: Exports ---
    script += jline(R"({"ts":)" + std::to_string(ts++) +
                    R"(,"action":"export_project_bibtex","project":"Session-Root","path":")" +
                    bib_path + R"("})");
    script += jline(R"({"ts":)" + std::to_string(ts++) +
                    R"(,"action":"export_project_markdown","project":"Session-Root","path":")" +
                    md_path + R"("})");

    // --- Phase 6: Clean up projects ---
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"remove_project","name":"Session-Child"})");
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"set_filter_index","index":0})");

    // --- Phase 7: Recommended filter ---
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"set_filter_index","index":5})");
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"set_article_index","index":0})");
    script += jline(R"({"ts":)" + std::to_string(ts++) + R"(,"action":"set_filter_index","index":0})");

    auto result = Arxiv::ReplayPlayer::FromString(script, *core);
    REQUIRE(result.error.empty());
    REQUIRE(result.replayed > 30);

    // Exported files were created
    REQUIRE(fs::exists(bib_path));
    REQUIRE(fs::exists(md_path));

    // Core is in a sane state
    core->TryRefetchIfNeeded();
    REQUIRE_FALSE(core->GetCurrentArticles().empty());
    REQUIRE(core->IsRankerTrained());

    fs::remove_all(tmp);
}
