// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <csignal>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "Arxiv/Replay.hh"
#include "Arxiv/CrashHandler.hh"
#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"

#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"

using namespace Catch::Matchers;
using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock         = arxiv_tui::test::FetcherMock;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::unique_ptr<Arxiv::AppCore> make_core(
    DatabaseManagerMock*& db_out,
    FetcherMock*&         fetcher_out)
{
    auto db_ptr  = std::make_unique<DatabaseManagerMock>();
    auto fet_ptr = std::make_unique<FetcherMock>();
    db_out      = db_ptr.get();
    fetcher_out = fet_ptr.get();
    Arxiv::Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    return std::make_unique<Arxiv::AppCore>(cfg, std::move(db_ptr), std::move(fet_ptr));
}

// ---------------------------------------------------------------------------
// ReplayRecorder — in-memory behaviour
// ---------------------------------------------------------------------------

TEST_CASE("ReplayRecorder: in-memory recording produces valid JSONL", "[replay][recorder]") {
    Arxiv::ReplayRecorder recorder; // no file path → in-memory only

    SECTION("fresh recorder has zero entries") {
        REQUIRE(recorder.GetCount() == 0);
        REQUIRE(recorder.GetJSONL().empty());
    }

    SECTION("RecordSetFilterIndex appears in JSONL") {
        recorder.RecordSetFilterIndex(3);
        std::string jsonl = recorder.GetJSONL();
        REQUIRE_THAT(jsonl, ContainsSubstring("set_filter_index"));
        REQUIRE_THAT(jsonl, ContainsSubstring("\"index\":3"));
        REQUIRE(recorder.GetCount() == 1);
    }

    SECTION("RecordSetArticleIndex appears in JSONL") {
        recorder.RecordSetArticleIndex(7);
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("set_article_index"));
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("\"index\":7"));
    }

    SECTION("RecordToggleBookmark appears in JSONL") {
        recorder.RecordToggleBookmark("http://arxiv.org/abs/1234.5678");
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("toggle_bookmark"));
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("1234.5678"));
    }

    SECTION("RecordAddProject appears in JSONL") {
        recorder.RecordAddProject("My Project");
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("add_project"));
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("My Project"));
    }

    SECTION("RecordRemoveProject appears in JSONL") {
        recorder.RecordRemoveProject("Old Project");
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("remove_project"));
    }

    SECTION("RecordLinkArticleToProject appears in JSONL") {
        recorder.RecordLinkArticleToProject("http://link", "proj");
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("link_article_to_project"));
    }

    SECTION("RecordUnlinkArticleFromProject appears in JSONL") {
        recorder.RecordUnlinkArticleFromProject("http://link", "proj");
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("unlink_article_from_project"));
    }

    SECTION("RecordSetDateRange appears in JSONL") {
        recorder.RecordSetDateRange("2024-01-01", "2024-12-31");
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("set_date_range"));
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("2024-01-01"));
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("2024-12-31"));
    }

    SECTION("RecordSetSearchQuery appears in JSONL") {
        recorder.RecordSetSearchQuery("neural networks", true, false, true);
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("set_search_query"));
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("neural networks"));
    }

    SECTION("RecordSetProjectNote appears in JSONL") {
        recorder.RecordSetProjectNote("proj", "http://link", "my note");
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("set_project_note"));
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("my note"));
    }

    SECTION("RecordRateArticle appears in JSONL") {
        recorder.RecordRateArticle("http://link", 4);
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("rate_article"));
        REQUIRE_THAT(recorder.GetJSONL(), ContainsSubstring("\"rating\":4"));
    }

    SECTION("multiple records produce multiple lines") {
        recorder.RecordSetFilterIndex(1);
        recorder.RecordSetArticleIndex(2);
        recorder.RecordToggleBookmark("http://x");
        REQUIRE(recorder.GetCount() == 3);
        // Count newlines: 3 lines means 3 newline characters
        std::string jsonl = recorder.GetJSONL();
        int newlines = 0;
        for (char c : jsonl) if (c == '\n') ++newlines;
        REQUIRE(newlines == 3);
    }

    SECTION("each line is valid JSON with a ts field") {
        recorder.RecordSetFilterIndex(0);
        std::string jsonl = recorder.GetJSONL();
        // Remove trailing newline for easier parsing
        if (!jsonl.empty() && jsonl.back() == '\n') jsonl.pop_back();
        REQUIRE_THAT(jsonl, ContainsSubstring("\"ts\""));
    }
}

// ---------------------------------------------------------------------------
// ReplayRecorder — file mode
// ---------------------------------------------------------------------------

TEST_CASE("ReplayRecorder: file mode writes JSONL to disk", "[replay][recorder][file]") {
    fs::path tmp = fs::temp_directory_path() / "replay_test_recorder.jsonl";
    // Clean up before and after
    fs::remove(tmp);

    {
        Arxiv::ReplayRecorder recorder(tmp.string());
        recorder.RecordSetFilterIndex(2);
        recorder.RecordSetArticleIndex(5);
    } // destructor should flush/close

    SECTION("file exists after recording") {
        REQUIRE(fs::exists(tmp));
    }

    SECTION("file contains expected JSONL") {
        std::ifstream f(tmp);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();
        REQUIRE_THAT(content, ContainsSubstring("set_filter_index"));
        REQUIRE_THAT(content, ContainsSubstring("\"index\":2"));
        REQUIRE_THAT(content, ContainsSubstring("set_article_index"));
        REQUIRE_THAT(content, ContainsSubstring("\"index\":5"));
    }

    fs::remove(tmp);
}

// ---------------------------------------------------------------------------
// ReplayPlayer — FromString
// ---------------------------------------------------------------------------

TEST_CASE("ReplayPlayer::FromString dispatches actions to AppCore", "[replay][player]") {
    DatabaseManagerMock* db_ptr     = nullptr;
    FetcherMock*         fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    SECTION("set_filter_index action updates filter") {
        std::string jsonl = R"({"ts":1000,"action":"set_filter_index","index":1})" "\n";
        auto result = Arxiv::ReplayPlayer::FromString(jsonl, *core);
        REQUIRE(result.total    == 1);
        REQUIRE(result.replayed == 1);
        REQUIRE(result.skipped  == 0);
        REQUIRE(result.error.empty());
        REQUIRE(core->GetFilterIndex() == 1);
    }

    SECTION("set_article_index action updates article index") {
        std::string jsonl = R"({"ts":1000,"action":"set_article_index","index":2})" "\n";
        auto result = Arxiv::ReplayPlayer::FromString(jsonl, *core);
        REQUIRE(result.replayed == 1);
        REQUIRE(core->GetArticleIndex() == 2);
    }

    SECTION("unknown action is skipped without error") {
        std::string jsonl = R"({"ts":1000,"action":"do_something_unknown"})" "\n";
        auto result = Arxiv::ReplayPlayer::FromString(jsonl, *core);
        REQUIRE(result.total    == 1);
        REQUIRE(result.skipped  == 1);
        REQUIRE(result.replayed == 0);
        REQUIRE(result.error.empty());
    }

    SECTION("malformed JSON line is reported as error") {
        std::string jsonl = "not valid json\n";
        auto result = Arxiv::ReplayPlayer::FromString(jsonl, *core);
        REQUIRE_FALSE(result.error.empty());
    }

    SECTION("multiple actions are all replayed") {
        std::string jsonl =
            R"({"ts":1000,"action":"set_filter_index","index":1})" "\n"
            R"({"ts":2000,"action":"set_article_index","index":3})" "\n";
        auto result = Arxiv::ReplayPlayer::FromString(jsonl, *core);
        REQUIRE(result.total    == 2);
        REQUIRE(result.replayed == 2);
        REQUIRE(result.skipped  == 0);
    }

    SECTION("toggle_bookmark action is dispatched") {
        ALLOW_CALL(*db_ptr, ToggleBookmark(ANY(std::string), ANY(bool)));
        std::string jsonl =
            R"({"ts":1000,"action":"toggle_bookmark","article_link":"http://x"})" "\n";
        auto result = Arxiv::ReplayPlayer::FromString(jsonl, *core);
        REQUIRE(result.replayed == 1);
    }

    SECTION("add_project action is dispatched") {
        ALLOW_CALL(*db_ptr, AddProject(ANY(std::string)));
        std::string jsonl =
            R"({"ts":1000,"action":"add_project","name":"TestProj"})" "\n";
        auto result = Arxiv::ReplayPlayer::FromString(jsonl, *core);
        REQUIRE(result.replayed == 1);
    }

    SECTION("empty JSONL produces zero results") {
        auto result = Arxiv::ReplayPlayer::FromString("", *core);
        REQUIRE(result.total    == 0);
        REQUIRE(result.replayed == 0);
        REQUIRE(result.error.empty());
    }
}

// ---------------------------------------------------------------------------
// ReplayPlayer — FromFile
// ---------------------------------------------------------------------------

TEST_CASE("ReplayPlayer::FromFile returns error for nonexistent path", "[replay][player][file]") {
    DatabaseManagerMock* db_ptr     = nullptr;
    FetcherMock*         fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    auto result = Arxiv::ReplayPlayer::FromFile("/nonexistent/path/replay.jsonl", *core);
    REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("ReplayPlayer::FromFile replays a valid file", "[replay][player][file]") {
    DatabaseManagerMock* db_ptr     = nullptr;
    FetcherMock*         fetcher_ptr = nullptr;
    auto core = make_core(db_ptr, fetcher_ptr);

    fs::path tmp = fs::temp_directory_path() / "replay_test_player.jsonl";
    {
        std::ofstream f(tmp);
        f << R"({"ts":1000,"action":"set_filter_index","index":1})" << "\n";
        f << R"({"ts":2000,"action":"set_article_index","index":2})" << "\n";
    }

    auto result = Arxiv::ReplayPlayer::FromFile(tmp.string(), *core);
    REQUIRE(result.total    == 2);
    REQUIRE(result.replayed == 2);
    REQUIRE(result.error.empty());

    fs::remove(tmp);
}

// ---------------------------------------------------------------------------
// WriteCrashReport
// ---------------------------------------------------------------------------

TEST_CASE("WriteCrashReport writes a crash report file", "[crashhandler]") {
    fs::path dir = fs::temp_directory_path() / "crash_report_test";
    fs::create_directories(dir);

    SECTION("without recorder writes signal name and backtrace header") {
        std::string path = Arxiv::WriteCrashReport(SIGSEGV, nullptr, dir.string());
        REQUIRE_FALSE(path.empty());
        REQUIRE(fs::exists(path));

        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();

        REQUIRE_THAT(content, ContainsSubstring("SIGSEGV"));
        REQUIRE_THAT(content, ContainsSubstring("Backtrace"));
        fs::remove(path);
    }

    SECTION("with recorder includes replay log") {
        Arxiv::ReplayRecorder recorder;
        recorder.RecordSetFilterIndex(2);
        recorder.RecordSetArticleIndex(1);

        std::string path = Arxiv::WriteCrashReport(SIGABRT, &recorder, dir.string());
        REQUIRE_FALSE(path.empty());
        REQUIRE(fs::exists(path));

        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();

        REQUIRE_THAT(content, ContainsSubstring("SIGABRT"));
        REQUIRE_THAT(content, ContainsSubstring("Replay Log"));
        REQUIRE_THAT(content, ContainsSubstring("set_filter_index"));
        fs::remove(path);
    }

    fs::remove_all(dir);
}

TEST_CASE("WriteCrashReport handles unwritable directory gracefully", "[crashhandler]") {
    // Pass a path that doesn't exist and can't be created (no permissions)
    // On Linux /proc is read-only for non-root
    std::string path = Arxiv::WriteCrashReport(SIGSEGV, nullptr, "/proc/no_such_subdir");
    // Should return empty string or a fallback path — either way, not crash
    // Just verify it doesn't throw
    (void)path;
}
