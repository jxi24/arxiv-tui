// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Clipboard.hh"

#include <array>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>

#include "spdlog/spdlog.h"

namespace Arxiv {

namespace {

// Probe whether a command is available on PATH without running it with input.
bool command_available(const char* name) {
    std::string check = std::string("command -v ") + name + " > /dev/null 2>&1";
    return std::system(check.c_str()) == 0;
}

// The ordered list of clipboard backends to probe.
constexpr std::array<const char*, 4> kBackends = {"wl-copy", "xclip", "xsel", "pbcopy"};

// Build the shell command that reads from stdin for the given backend.
std::string pipe_command(const std::string& backend) {
    if (backend == "xclip")
        return "xclip -selection clipboard";
    if (backend == "xsel")
        return "xsel --clipboard --input";
    // wl-copy and pbcopy read stdin directly.
    return backend;
}

} // namespace

std::string Clipboard::DetectBackend(const std::string& config_backend) {
    // 1. Explicit config value (passed by the caller).
    if (!config_backend.empty())
        return config_backend;

    // 2. Environment variable override.
    const char* env = std::getenv("ARXIV_TUI_CLIPBOARD");
    if (env && *env)
        return env;

    // 3. First available system backend.
    for (const char* name : kBackends) {
        if (command_available(name))
            return name;
    }

    return "";
}

bool Clipboard::Copy(const std::string& text, const std::string& backend) {
    std::string resolved = DetectBackend(backend);
    if (resolved.empty()) {
        spdlog::warn("[Clipboard]: No clipboard backend available");
        return false;
    }

    std::string cmd = pipe_command(resolved) + " 2>/dev/null";
    FILE* pipe = ::popen(cmd.c_str(), "w");
    if (!pipe) {
        spdlog::warn("[Clipboard]: Failed to open pipe to '{}'", resolved);
        return false;
    }

    // Ignore SIGPIPE for the entire pipe lifetime: fwrite and the implicit
    // fflush inside pclose can both write to the broken pipe if the backend
    // command exits early. Restore the previous handler only after pclose.
    auto old_sigpipe = std::signal(SIGPIPE, SIG_IGN);
    std::fwrite(text.c_str(), 1, text.size(), pipe);
    int ret = ::pclose(pipe);
    if (old_sigpipe != SIG_ERR)
        std::signal(SIGPIPE, old_sigpipe);
    if (ret != 0) {
        spdlog::warn("[Clipboard]: Backend '{}' exited with status {}", resolved, ret);
        return false;
    }

    spdlog::debug("[Clipboard]: Copied {} bytes via '{}'", text.size(), resolved);
    return true;
}

} // namespace Arxiv
