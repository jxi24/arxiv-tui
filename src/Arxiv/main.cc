#include "Arxiv/App.hh"
#include "Arxiv/Config.hh"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

void CreateLogger(int level, int flush_time) {
    auto slevel = static_cast<spdlog::level::level_enum>(level);
    auto logger = spdlog::basic_logger_mt("arxiv", "arxiv_tui.log");
    logger->set_level(slevel);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(flush_time));
    spdlog::set_pattern("[%^%l%$] %v");
}

int main() {
    CreateLogger(0, 1);
    // try {
        Arxiv::Config config(".arxiv-tui.yml");
        Arxiv::ArxivApp app(config);
        app.Run();
    // } catch(const std::exception &e) {
    //     spdlog::error("[Arxiv-TUI]: Unhandled exception {}", e.what());
    //     return 1;
    // }

    return 0;
}
