#include "Arxiv/App.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/Replay.hh"
#include "Arxiv/CrashHandler.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include <cstring>
#include <iostream>
#include <string>

void CreateLogger(int level, int flush_time) {
    auto slevel = static_cast<spdlog::level::level_enum>(level);
    auto logger = spdlog::basic_logger_mt("arxiv", "arxiv_tui.log");
    logger->set_level(slevel);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(flush_time));
    spdlog::set_pattern("[%^%l%$] %v");
}

int main(int argc, char* argv[]) {
    CreateLogger(0, 1);

    // Parse CLI flags
    std::string replay_file;
    std::string export_today_path;
    std::string export_yaml_path;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_file = argv[++i];
        } else if (std::strcmp(argv[i], "--export-today") == 0 && i + 1 < argc) {
            export_today_path = argv[++i];
        } else if (std::strcmp(argv[i], "--export-yaml") == 0 && i + 1 < argc) {
            export_yaml_path = argv[++i];
        }
    }

    Arxiv::Config config(".arxiv-tui.yml");

    // Headless digest export
    if (!export_today_path.empty() || !export_yaml_path.empty()) {
        auto core = std::make_unique<Arxiv::AppCore>(
            config,
            std::make_unique<Arxiv::DatabaseManager>("articles.db"),
            std::make_unique<Arxiv::Fetcher>(config.get_topics(), config.get_download_dir()));

        if (!export_today_path.empty()) {
            bool ok = core->ExportDailyDigest(export_today_path);
            std::cout << (ok ? "Digest written to " + export_today_path : "Failed to write digest") << "\n";
            if (!ok) return 1;
        }
        if (!export_yaml_path.empty()) {
            bool ok = core->ExportDailyDigestYAML(export_yaml_path);
            std::cout << (ok ? "YAML digest written to " + export_yaml_path : "Failed to write YAML digest") << "\n";
            if (!ok) return 1;
        }
        return 0;
    }

    if (!replay_file.empty()) {
        // Headless replay mode: no TUI, just dispatch actions to AppCore
        spdlog::info("Replay mode: replaying from {}", replay_file);
        auto core = std::make_unique<Arxiv::AppCore>(
            config,
            std::make_unique<Arxiv::DatabaseManager>("articles.db"),
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
    Arxiv::ReplayRecorder recorder("replay.jsonl");
    Arxiv::InstallCrashHandler(&recorder, ".");

    Arxiv::ArxivApp app(config, ".arxiv-tui.yml", &recorder);
    recorder.RecordEvent("main/run_loop_start");
    app.Run();
    recorder.RecordEvent("main/run_loop_returned");

    return 0;
}
