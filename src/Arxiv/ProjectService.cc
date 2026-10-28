// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/ProjectService.hh"

#include "Arxiv/AtomicFile.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Export.hh"
#include "Arxiv/Fetcher.hh"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "spdlog/spdlog.h"

namespace Arxiv {

ProjectService::ProjectService(DatabaseManager& db, BibTeXFor bibtex)
    : m_db(db)
    , m_bibtex(std::move(bibtex)) {}

std::optional<std::string> ProjectService::Add(const std::string& name) {
    const auto first = name.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return std::nullopt;
    const auto last = name.find_last_not_of(" \t\n\r");
    std::string trimmed = name.substr(first, last - first + 1);
    m_db.AddProject(trimmed);
    return trimmed;
}

void ProjectService::Remove(const std::string& name) { m_db.RemoveProject(name); }

void ProjectService::SetParent(const std::string& name, const std::string& parent) {
    m_db.SetProjectParent(name, parent);
}

void ProjectService::Link(const std::string& project, const std::vector<std::string>& links) {
    for (const auto& link : links)
        m_db.LinkArticleToProject(link, project);
    AppendToBib(project, links);
}

void ProjectService::AppendToBib(const std::string& project,
                                 const std::vector<std::string>& links) {
    const std::string bib_path = m_db.GetProjectBibPath(project);
    if (bib_path.empty() || !std::filesystem::exists(bib_path))
        return;
    std::string content;
    {
        std::ifstream existing(bib_path);
        content.assign(std::istreambuf_iterator<char>(existing), std::istreambuf_iterator<char>());
    }
    bool changed = false;
    for (const auto& link : links) {
        const auto article = m_db.GetArticle(link);
        if (!article)
            continue;
        // Already there (re-linking used to add it again)?
        const std::string id = article->id();
        if (content.find("{" + id + "}") != std::string::npos ||
            content.find("\"" + id + "\"") != std::string::npos)
            continue;
        content += "\n" + m_bibtex(*article);
        changed = true;
    }
    if (changed && WriteFileAtomically(bib_path, content))
        spdlog::info("[Projects]: Updated {} for project {}", bib_path, project);
}

void ProjectService::Unlink(const std::string& project, const std::string& link) {
    m_db.UnlinkArticleFromProject(link, project);
}

std::vector<std::string> ProjectService::All() const { return m_db.GetProjects(); }

std::vector<Article> ProjectService::Articles(const std::string& project) const {
    return m_db.GetArticlesForProject(project);
}

std::vector<std::string> ProjectService::ProjectsFor(const std::string& link) const {
    return m_db.GetProjectsForArticle(link);
}

void ProjectService::SetNote(const std::string& project,
                             const std::string& link,
                             const std::string& note) {
    m_db.SetProjectNote(project, link, note);
}

std::string ProjectService::Note(const std::string& project, const std::string& link) const {
    return m_db.GetProjectNote(project, link);
}

std::function<std::string(const std::string&)>
ProjectService::NotesFor(const std::string& project) const {
    return [this, project](const std::string& link) { return m_db.GetProjectNote(project, link); };
}

bool ProjectService::ExportMarkdown(const std::string& project, const std::string& path) const {
    return Export::ProjectMarkdown(path, project, Articles(project), NotesFor(project));
}

bool ProjectService::ExportText(const std::string& project, const std::string& path) const {
    return Export::ProjectText(path, project, Articles(project), NotesFor(project));
}

bool ProjectService::ExportJSON(const std::string& project, const std::string& path) const {
    Export::ProjectExtras extras;
    extras.parent = m_db.GetProjectParent(project);
    extras.tags_for = [this](const std::string& link) { return m_db.GetTagsForArticle(link); };
    extras.rating_for = [this](const std::string& link) { return m_db.GetRating(link); };
    return Export::ProjectJSON(path, project, Articles(project), NotesFor(project), extras);
}

bool ProjectService::ExportArticlesBibTeX(const std::vector<Article>& articles,
                                          const std::string& path) const {
    return Export::BibTeXFile(path, articles, m_bibtex);
}

bool ProjectService::ExportBibTeX(const std::string& project, const std::string& path) {
    const bool ok = ExportArticlesBibTeX(Articles(project), path);
    if (ok)
        m_db.SetProjectBibPath(project, path);
    return ok;
}

std::optional<std::string> ProjectService::ImportJSON(const std::string& path) {
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (!size_error && size > kMaxImportBytes) {
        spdlog::error("[Projects]: Import file {} is larger than {} bytes", path, kMaxImportBytes);
        return std::nullopt;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::error("[Projects]: Cannot open import file: {}", path);
        return std::nullopt;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::exception& e) {
        // parse_error, or out_of_range for numbers like 1e70000
        spdlog::error("[Projects]: JSON parse error: {}", e.what());
        return std::nullopt;
    }
    if (!j.contains("name") || !j["name"].is_string()) {
        spdlog::error("[Projects]: Import JSON missing 'name' field");
        return std::nullopt;
    }

    // Trimmed like a typed name; a blank one could never be selected.
    std::string project = j["name"].get<std::string>();
    const auto first = project.find_first_not_of(" \t");
    if (first == std::string::npos) {
        spdlog::error("[Projects]: Import JSON has a blank project name");
        return std::nullopt;
    }
    project = project.substr(first, project.find_last_not_of(" \t") - first + 1);
    m_db.AddProject(project);
    if (auto parent = j.find("parent"); parent != j.end() && parent->is_string() &&
                                        !parent->get<std::string>().empty() &&
                                        parent->get<std::string>() != project)
        m_db.SetProjectParent(project, parent->get<std::string>());

    // The file is untrusted: skip entries that are not objects, ignore fields
    // of the wrong type, and only accept canonical arXiv abstract links (they
    // end up in file names and are handed to xdg-open).
    auto string_field = [](const nlohmann::json& entry, const char* key) {
        auto it = entry.find(key);
        return (it != entry.end() && it->is_string()) ? it->get<std::string>() : std::string();
    };
    std::size_t skipped = 0;
    if (j.contains("articles") && j["articles"].is_array()) {
        for (const auto& entry : j["articles"]) {
            if (!entry.is_object()) {
                ++skipped;
                continue;
            }
            const std::string link = Fetcher::NormalizeLink(string_field(entry, "link"));
            if (!Fetcher::IsArxivLink(link)) {
                ++skipped;
                continue;
            }
            // A paper already in the database keeps its stored metadata (the
            // file has no category and may be old or hand-edited).
            if (!m_db.GetArticle(link)) {
                Article article;
                article.link = link;
                article.title = string_field(entry, "title");
                article.authors = string_field(entry, "authors");
                article.abstract = string_field(entry, "abstract");
                article.category = string_field(entry, "category");
                article.bookmarked = false;
                // No date in the file: now, rather than 1 January 1970.
                article.date = std::chrono::system_clock::now();
                auto date = entry.find("date");
                if (date != entry.end() && date->is_number_integer())
                    article.date = std::chrono::system_clock::from_time_t(date->get<std::time_t>());
                m_db.AddArticle(article);
            }
            m_db.LinkArticleToProject(link, project);
            if (auto tags = entry.find("tags"); tags != entry.end() && tags->is_array())
                for (const auto& tag : *tags)
                    if (tag.is_string() && !tag.get<std::string>().empty())
                        m_db.LinkArticleToTag(link, tag.get<std::string>());
            // A rating from the file never overrides the user's own.
            if (auto rating = entry.find("rating");
                rating != entry.end() && rating->is_number_integer() && m_db.GetRating(link) == 0) {
                const auto r = rating->get<long long>();
                if (r >= 1 && r <= 5)
                    m_db.SetRating(link, static_cast<int>(r));
            }
            const std::string note = string_field(entry, "note");
            if (!note.empty())
                m_db.SetProjectNote(project, link, note);
        }
    }
    if (skipped > 0)
        spdlog::warn("[Projects]: Import skipped {} malformed article entr(y/ies)", skipped);
    spdlog::info("[Projects]: Imported project '{}' from JSON: {}", project, path);
    return project;
}

} // namespace Arxiv
