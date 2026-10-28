// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>

namespace Arxiv {

struct Article;

namespace BibTeX {

/// Escape text for a BibTeX field value: %, & and # get a backslash (unless
/// already escaped) and, if the braces do not balance, every brace is escaped
/// so the entry stays parseable. LaTeX commands are left alone.
std::string Escape(const std::string& text);

/// "<LastName>:<year>_<id>" using only [A-Za-z0-9:._-].
std::string CiteKey(const Article& article);

/// A complete @article entry built from the article's own metadata (used
/// when InspireHEP has no record). The year is the UTC year of `date`.
std::string Build(const Article& article);

} // namespace BibTeX
} // namespace Arxiv
