// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Clipboard.hh"

#include "Arxiv/Process.hh"

#include <array>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "spdlog/spdlog.h"

namespace Arxiv {

namespace {

// True if `name` is an executable file in a PATH directory. No shell.
bool command_available(const std::string& name) {
    const char* path = std::getenv("PATH");
    if (!path)
        return false;
    std::stringstream dirs(path);
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
        if (dir.empty())
            continue;
        if (::access((dir + "/" + name).c_str(), X_OK) == 0)
            return true;
    }
    return false;
}

// The ordered list of clipboard backends to probe.
constexpr std::array<const char*, 4> kBackends = {"wl-copy", "xclip", "xsel", "pbcopy"};

// The argument vector that makes `backend` read the clipboard text from
// stdin. Known backends get their flags; anything else is a program name
// optionally followed by arguments, split on whitespace — never a shell
// command line.
std::vector<std::string> backend_argv(const std::string& backend) {
    if (backend == "xclip")
        return {"xclip", "-selection", "clipboard"};
    if (backend == "xsel")
        return {"xsel", "--clipboard", "--input"};
    std::vector<std::string> argv;
    std::istringstream words(backend);
    std::string word;
    while (words >> word)
        argv.push_back(word);
    return argv;
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
    auto argv = backend_argv(resolved);
    if (argv.empty()) {
        spdlog::warn("[Clipboard]: No clipboard backend available");
        return false;
    }

    int ret = Process::RunWithInput(argv, text);
    if (ret != 0) {
        spdlog::warn("[Clipboard]: Backend '{}' exited with status {}", resolved, ret);
        return false;
    }

    spdlog::debug("[Clipboard]: Copied {} bytes via '{}'", text.size(), resolved);
    return true;
}

} // namespace Arxiv
