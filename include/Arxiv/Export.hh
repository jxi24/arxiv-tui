// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Arxiv/Article.hh"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Arxiv {

class Fetcher;

/// File exports. These know nothing about the database, the selection or the
/// UI: callers pass the articles (and anything else) in. Each returns false /
/// an empty path if the output could not be written in full.
namespace Export {

/// Note text for an article link ("" if none).
using NoteLookup = std::function<std::string(const std::string& link)>;
/// Called once per article as a long export makes progress (may be empty).
using Progress = std::function<void()>;

bool ProjectMarkdown(const std::string& path,
                     const std::string& project,
                     const std::vector<Article>& articles,
                     const NoteLookup& note_for);
bool ProjectText(const std::string& path,
                 const std::string& project,
                 const std::vector<Article>& articles,
                 const NoteLookup& note_for);
bool ProjectJSON(const std::string& path,
                 const std::string& project,
                 const std::vector<Article>& articles,
                 const NoteLookup& note_for);

bool DailyDigestMarkdown(const std::string& path, const std::vector<Article>& articles);
/// `scored` in the order to write, each with its predicted score.
bool DailyDigestYAML(const std::string& path, const std::vector<std::pair<float, Article>>& scored);

/// <download_dir>/<date>/digest.md plus each article's PDF beside it.
/// Returns the digest directory.
std::string SelectedDigest(Fetcher& fetcher,
                           const std::vector<Article>& articles,
                           const std::string& download_dir,
                           const Progress& progress);

/// One note per article under <vault>/arxiv-tui/<date>/ and an index note
/// linking them; PDFs are downloaded beside the notes only when asked.
/// Returns the index note's path.
std::string ObsidianNotes(Fetcher& fetcher,
                          const std::vector<Article>& articles,
                          const std::string& vault,
                          bool download_pdfs,
                          const Progress& progress);

/// One BibTeX entry per article, as produced by `entry_for`.
bool BibTeXFile(const std::string& path,
                const std::vector<Article>& articles,
                const std::function<std::string(const Article&)>& entry_for);

/// Pack a digest directory into <dir>.tar.gz beside it; returns its path.
std::string PackArchive(const std::string& dir);

/// Today's local date, "YYYY-MM-DD" (used in export names and headings).
std::string Today();

} // namespace Export
} // namespace Arxiv
