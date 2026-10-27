// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "spdlog/logger.h"

namespace Arxiv {

/// The app's rotating log file (5 MB, 3 rotated files). Created mode 0600
/// and close-on-exec: it holds search queries and paths.
std::shared_ptr<spdlog::logger>
MakeFileLogger(const std::string& name, const std::filesystem::path& path, bool trace);

} // namespace Arxiv
