// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Arxiv/Article.hh"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Arxiv {

class DatabaseManager;

/// Projects as data: creating and removing them, membership (keeping an
/// exported .bib in step), notes, exports and JSON import. No view state —
/// AppCore refreshes the filter list and the article list around these calls.
class ProjectService {
  public:
    /// BibTeX for one article (InspireHEP or the fallback builder).
    using BibTeXFor = std::function<std::string(const Article&)>;

    /// Project import files larger than this are refused unread.
    static constexpr std::uintmax_t kMaxImportBytes = std::uintmax_t{64} << 20;

    ProjectService(DatabaseManager& db, BibTeXFor bibtex);

    /// Create a project; the name is trimmed. Returns the stored name, or
    /// nullopt if nothing is left after trimming.
    std::optional<std::string> Add(const std::string& name);
    void Remove(const std::string& name);
    void SetParent(const std::string& name, const std::string& parent);

    /// Add articles to a project. If the project's .bib was exported before,
    /// articles not yet in it are appended.
    void Link(const std::string& project, const std::vector<std::string>& links);
    void Unlink(const std::string& project, const std::string& link);

    std::vector<std::string> All() const;
    std::vector<Article> Articles(const std::string& project) const;
    std::vector<std::string> ProjectsFor(const std::string& link) const;

    void SetNote(const std::string& project, const std::string& link, const std::string& note);
    std::string Note(const std::string& project, const std::string& link) const;

    bool ExportMarkdown(const std::string& project, const std::string& path) const;
    bool ExportText(const std::string& project, const std::string& path) const;
    bool ExportJSON(const std::string& project, const std::string& path) const;
    /// Also remembers `path` as the project's .bib, which Link keeps current.
    bool ExportBibTeX(const std::string& project, const std::string& path);
    bool ExportArticlesBibTeX(const std::vector<Article>& articles, const std::string& path) const;

    /// Import a project exported as JSON. The file is untrusted: malformed
    /// entries and non-arXiv links are skipped, and papers already stored
    /// keep their metadata. Returns the project name, or nullopt on failure.
    std::optional<std::string> ImportJSON(const std::string& path);

  private:
    std::function<std::string(const std::string&)> NotesFor(const std::string& project) const;
    void AppendToBib(const std::string& project, const std::vector<std::string>& links);

    DatabaseManager& m_db;
    BibTeXFor m_bibtex;
};

} // namespace Arxiv
