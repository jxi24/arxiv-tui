// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>

namespace Arxiv {

/// System clipboard integration.
///
/// Backend resolution order (highest to lowest priority):
///   1. config value passed to DetectBackend / Copy
///   2. ARXIV_TUI_CLIPBOARD environment variable
///   3. First available among: wl-copy, xclip, xsel, pbcopy
class Clipboard {
  public:
    /// Determine which clipboard backend to use.  Returns the resolved backend
    /// name, or an empty string if none is available.
    static std::string DetectBackend(const std::string& config_backend);

    /// Copy text to the system clipboard using the resolved backend.
    /// Returns true on success, false if no backend is available or the
    /// backend command fails.
    static bool Copy(const std::string& text, const std::string& backend = "");
};

} // namespace Arxiv
