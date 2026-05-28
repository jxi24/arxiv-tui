// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>

namespace Arxiv {

// Strip LaTeX markup from text, returning plain prose suitable for NLP.
//
// Rules applied in order:
//   1. $$...$$ display-math regions → replaced with a space
//   2. $...$ inline-math regions   → replaced with a space
//   3. \cmd{content}               → replaced with a space + content
//   4. bare \cmd tokens            → replaced with a space
std::string StripLatex(const std::string& text);

} // namespace Arxiv
