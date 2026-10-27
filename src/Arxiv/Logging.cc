// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Logging.hh"

#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>

#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/spdlog.h"

namespace Arxiv {

std::shared_ptr<spdlog::logger>
MakeFileLogger(const std::string& name, const std::filesystem::path& path, bool trace) {
    constexpr std::size_t kMaxBytes = 5 * 1024 * 1024;
    constexpr std::size_t kMaxFiles = 3;
    spdlog::file_event_handlers private_file;
    private_file.after_open = [](const spdlog::filename_t&, std::FILE* file) {
        const int fd = ::fileno(file);
        ::fchmod(fd, 0600);
        ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
    };
    auto logger =
        spdlog::rotating_logger_mt(name, path.string(), kMaxBytes, kMaxFiles, false, private_file);
    logger->set_level(trace ? spdlog::level::trace : spdlog::level::debug);
    logger->flush_on(spdlog::level::warn);
    return logger;
}

std::string ForLog(std::string_view untrusted) {
    // Control characters are shown escaped: ESC would run in the terminal
    // of whoever tails the log, and a newline would start a forged line.
    constexpr std::size_t kMaxShown = 200;
    std::string out;
    const std::size_t n = std::min(untrusted.size(), kMaxShown);
    for (std::size_t i = 0; i < n; ++i) {
        const auto c = static_cast<unsigned char>(untrusted[i]);
        if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c < 0x20 || c == 0x7f)
            out += fmt::format("\\x{:02x}", c);
        else
            out += static_cast<char>(c);
    }
    if (untrusted.size() > kMaxShown)
        out += "...";
    return out;
}

} // namespace Arxiv
