// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Cli.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/CrashHandler.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Paths.hh"
#include "Arxiv/Replay.hh"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>

#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/spdlog.h"

// Timestamp + PID so a replay file and log from the same run can be matched
// by grepping both for the session ID.
static std::string MakeSessionId() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return std::string(buf) + "-" + std::to_string(getpid());
}

// Rotating logger: up to 5 MB per file, keep 3 rotated files.
// Default level is debug; --trace enables trace-level output.
void CreateLogger(bool trace_mode, const std::filesystem::path& log_path) {
    constexpr std::size_t kMaxBytes = 5 * 1024 * 1024;
    constexpr std::size_t kMaxFiles = 3;
    auto logger = spdlog::rotating_logger_mt("arxiv", log_path.string(), kMaxBytes, kMaxFiles);
    logger->set_level(trace_mode ? spdlog::level::trace : spdlog::level::debug);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(5));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
}

int main(int argc, char* argv[]) {
    // Parse CLI flags before creating the logger so --trace takes effect.
    const Arxiv::Cli::Options cli = Arxiv::Cli::Parse(argc, argv);
    if (!cli.error.empty()) {
        std::cerr << "arxiv-tui: " << cli.error << "\nTry 'arxiv-tui --help'.\n";
        return 2;
    }
    if (cli.mode == Arxiv::Cli::Options::Mode::Help) {
        std::cout << Arxiv::Cli::Usage();
        return 0;
    }
    if (cli.mode == Arxiv::Cli::Options::Mode::Version) {
        std::cout << Arxiv::Cli::VersionText();
        return 0;
    }
    const std::string& replay_file = cli.replay_file;
    const std::string& export_today_path = cli.export_today;
    const std::string& export_yaml_path = cli.export_yaml;
    const bool trace_mode = cli.trace;
    const bool fetch_only = cli.mode == Arxiv::Cli::Options::Mode::Fetch;

    // Resolve all runtime paths from XDG / install prefix before anything else.
    const Arxiv::Paths paths = Arxiv::Paths::Resolve();
    std::filesystem::create_directories(paths.config_file.parent_path());
    std::filesystem::create_directories(paths.data_dir);
    std::filesystem::create_directories(paths.state_dir);

    CreateLogger(trace_mode, paths.state_dir / "arxiv_tui.log");

    // Bad values (or an unparseable file) fall back to the defaults; the TUI
    // shows the warnings in a pop-up, headless modes print them.
    Arxiv::Config config(paths.config_file.string());
    const bool headless = fetch_only || !export_today_path.empty() || !export_yaml_path.empty() ||
                          !replay_file.empty();
    if (headless)
        for (const auto& w : config.get_warnings())
            std::cerr << "config: " << w << "\n";

    // Apply resolved defaults for any path that was not explicitly set in the
    // config file (empty string is the sentinel for "not configured").
    if (config.get_download_dir().empty())
        config.set_download_dir((paths.data_dir / "downloads").string());
    config.set_db_file((paths.data_dir / "articles.db").string());
    config.set_ranker_file((paths.data_dir / "ranker.bin").string());
    if (config.get_export_dir().empty())
        config.set_export_dir((paths.data_dir / "exports").string());
    if (config.get_keywords_file().empty())
        config.set_keywords_file((paths.data_dir / "keywords.txt").string());

    // Headless feed fetch: update the DB and exit without opening the TUI.
    // Suitable for use in a cron job to keep the database current.
    // Same fetch logic as the TUI (backfill of missed days, fetch
    // bookkeeping, pruning), so a cron job keeps the database exactly as an
    // interactive session would.
    if (fetch_only) {
        spdlog::info("Fetch-only mode: fetching articles");
        try {
            Arxiv::AppCore core(
                config,
                std::make_unique<Arxiv::DatabaseManager>(config.get_db_file()),
                std::make_unique<Arxiv::Fetcher>(config.get_topics(), config.get_download_dir()));
            if (!core.LastFetchSucceeded()) {
                std::cerr << "Fetch failed; see the log for details.\n";
                return 1;
            }
            std::cout << "Fetch complete.\n";
        } catch (const std::exception& e) {
            std::cerr << "Fetch failed: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    // Headless digest export
    if (!export_today_path.empty() || !export_yaml_path.empty()) {
        auto core = std::make_unique<Arxiv::AppCore>(
            config,
            std::make_unique<Arxiv::DatabaseManager>(config.get_db_file()),
            std::make_unique<Arxiv::Fetcher>(config.get_topics(), config.get_download_dir()),
            Arxiv::AppCore::FetchMode::Offline); // read-only: no fetch, no anchor change
        core->ReloadKeywords();                  // interest keywords feed the YAML digest's ranking

        if (!export_today_path.empty()) {
            bool ok = core->ExportDailyDigest(export_today_path);
            std::cout << (ok ? "Digest written to " + export_today_path : "Failed to write digest")
                      << "\n";
            if (!ok)
                return 1;
        }
        if (!export_yaml_path.empty()) {
            bool ok = core->ExportDailyDigestYAML(export_yaml_path);
            std::cout << (ok ? "YAML digest written to " + export_yaml_path
                             : "Failed to write YAML digest")
                      << "\n";
            if (!ok)
                return 1;
        }
        return 0;
    }

    if (!replay_file.empty()) {
        spdlog::info("Replay mode: replaying from {}", replay_file);
        // A replay never changes the user's data: it runs on an in-memory
        // copy of the database, with the ranker and keywords copied to a
        // scratch directory, and skips actions that write files or download.
        namespace fs = std::filesystem;
        const fs::path scratch =
            fs::temp_directory_path() / ("arxiv-tui-replay-" + std::to_string(::getpid()));
        fs::create_directories(scratch);
        std::error_code ignore;
        for (auto [from, to] : {std::pair{config.get_ranker_file(), scratch / "ranker.bin"},
                                std::pair{config.get_keywords_file(), scratch / "keywords.txt"}})
            fs::copy_file(from, to, fs::copy_options::overwrite_existing, ignore);
        config.set_ranker_file((scratch / "ranker.bin").string());
        config.set_keywords_file((scratch / "keywords.txt").string());
        config.set_download_dir((scratch / "downloads").string());

        Arxiv::ReplayPlayer::Result result;
        try {
            Arxiv::AppCore core(
                config,
                Arxiv::DatabaseManager::InMemoryCopyOf(config.get_db_file()),
                std::make_unique<Arxiv::Fetcher>(config.get_topics(), config.get_download_dir()),
                Arxiv::AppCore::FetchMode::Offline);
            result = Arxiv::ReplayPlayer::FromFile(replay_file, core);
        } catch (const std::exception& e) {
            result.error = e.what();
        }
        fs::remove_all(scratch, ignore);
        std::cout << "Replay complete: " << result.replayed << "/" << result.total
                  << " actions replayed, " << result.skipped << " skipped.\n";
        if (!result.error.empty()) {
            std::cerr << "Replay error: " << result.error << "\n";
            return 1;
        }
        return 0;
    }

    // Normal TUI mode
    const std::string session_id = MakeSessionId();
    spdlog::info("Session {} started", session_id);

    const std::string replay_path = (paths.state_dir / "replay.jsonl").string();
    Arxiv::ReplayRecorder recorder(replay_path);
    recorder.RecordEvent("session_start", "id=" + session_id);
    Arxiv::InstallCrashHandler(&recorder, paths.state_dir.string());

    Arxiv::ArxivApp app(config, paths.config_file.string(), &recorder);
    recorder.RecordEvent("main/run_loop_start");
    app.Run();
    recorder.RecordEvent("main/run_loop_returned");

    return 0;
}
