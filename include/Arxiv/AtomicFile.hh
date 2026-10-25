// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>
#include <string_view>

namespace Arxiv {

/// Replace the file at `path` with `content` so that, whatever happens, the
/// file holds either its old content or all of the new: the bytes go to a
/// temporary file in the same directory, are fsync'd, and are renamed over
/// `path`. A symlink at `path` is followed (the link itself is kept); a
/// device or FIFO is written in place. Returns false — leaving an old regular
/// file untouched — if any step fails.
bool WriteFileAtomically(const std::filesystem::path& path, std::string_view content);

} // namespace Arxiv
