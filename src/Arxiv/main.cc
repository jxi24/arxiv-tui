// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/CrashHandler.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Paths.hh"
#include "Arxiv/Replay.hh"

#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <unistd.h>

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
    // Parse CLI flags before creating the logger so --trace takes effect
    std::string replay_file;
    std::string export_today_path;
    std::string export_yaml_path;
    bool trace_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_file = argv[++i];
        } else if (std::strcmp(argv[i], "--export-today") == 0 && i + 1 < argc) {
            export_today_path = argv[++i];
        } else if (std::strcmp(argv[i], "--export-yaml") == 0 && i + 1 < argc) {
            export_yaml_path = argv[++i];
        } else if (std::strcmp(argv[i], "--trace") == 0) {
            trace_mode = true;
        }
    }

    // Resolve all runtime paths from XDG / install prefix before anything else.
    const Arxiv::Paths paths = Arxiv::Paths::Resolve();
    std::filesystem::create_directories(paths.config_file.parent_path());
    std::filesystem::create_directories(paths.data_dir);
    std::filesystem::create_directories(paths.state_dir);

    CreateLogger(trace_mode, paths.state_dir / "arxiv_tui.log");

    Arxiv::Config config(paths.config_file.string());

    // Apply resolved defaults for any path that was not explicitly set in the
    // config file (empty string is the sentinel for "not configured").
    if (config.get_download_dir().empty())
        config.set_download_dir((paths.data_dir / "downloads").string());
    config.set_db_file((paths.data_dir / "articles.db").string());
    config.set_ranker_file((paths.data_dir / "ranker.bin").string());

    // Headless digest export
    if (!export_today_path.empty() || !export_yaml_path.empty()) {
        auto core = std::make_unique<Arxiv::AppCore>(
            config,
            std::make_unique<Arxiv::DatabaseManager>(config.get_db_file()),
            std::make_unique<Arxiv::Fetcher>(config.get_topics(), config.get_download_dir()));

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
        auto core = std::make_unique<Arxiv::AppCore>(
            config,
            std::make_unique<Arxiv::DatabaseManager>(config.get_db_file()),
            std::make_unique<Arxiv::Fetcher>(config.get_topics(), config.get_download_dir()));

        auto result = Arxiv::ReplayPlayer::FromFile(replay_file, *core);
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
