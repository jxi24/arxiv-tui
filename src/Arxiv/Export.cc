// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Export.hh"

#include "Arxiv/AtomicFile.hh"
#include "Arxiv/Config.hh" // yaml-cpp: included via a project header, see CLAUDE.md
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Process.hh"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <unistd.h>

#include "fmt/format.h"
#include "spdlog/spdlog.h"

namespace Arxiv::Export {

namespace {

// An article's arXiv day (dates are stored as UTC instants).
std::string format_date(const std::chrono::system_clock::time_point& tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = {};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

// Open, write, and check the stream afterwards: a full disk used to report
// success because only the open was checked.
bool write_file(const std::string& path,
                std::string_view log_subject,
                const std::function<void(std::ostream&)>& writer) {
    // Fail before building the content: a BibTeX writer looks every paper up
    // on InspireHEP, which is wasted on a path that cannot be written.
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path target(path);
    const bool special = fs::exists(target, ec) && !fs::is_regular_file(target, ec);
    const fs::path dir = target.has_parent_path() ? target.parent_path() : fs::path(".");
    if (!special && ::access(dir.c_str(), W_OK) != 0) {
        spdlog::error(
            "[Export]: Cannot write {} to {}: {}", log_subject, path, std::strerror(errno));
        return false;
    }
    std::ostringstream file;
    writer(file);
    if (!WriteFileAtomically(path, file.str())) {
        spdlog::error("[Export]: Writing {} to {} failed", log_subject, path);
        return false;
    }
    spdlog::info("[Export]: Exported {} to {}", log_subject, path);
    return true;
}

// Quote a string for YAML frontmatter — wrap in double quotes and escape
// embedded backslashes / double quotes. Newlines are folded to spaces so a
// single-line value remains valid in flow style; other control characters
// (feed text can carry ESC) are dropped, as YAML does not allow them raw.
std::string yaml_quote(const std::string& s) {
    std::string out = "\"";
    out.reserve(s.size() + 2);
    for (char c : s) {
        if (c == '\\')
            out += "\\\\";
        else if (c == '"')
            out += "\\\"";
        else if (c == '\n' || c == '\r' || c == '\t')
            out += ' ';
        else if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            continue;
        else
            out += c;
    }
    out += '"';
    return out;
}

// Split a comma-separated category string into a YAML inline list of quoted
// items: "hep-ph, hep-lat" -> ["hep-ph", "hep-lat"]. Unquoted, feed text with
// a newline could add frontmatter keys.
std::string yaml_inline_list(const std::string& csv) {
    std::string out = "[";
    bool first = true;
    std::string token;
    auto flush = [&]() {
        // trim
        size_t a = token.find_first_not_of(" \t");
        size_t b = token.find_last_not_of(" \t");
        if (a == std::string::npos) {
            token.clear();
            return;
        }
        std::string t = token.substr(a, b - a + 1);
        token.clear();
        if (t.empty())
            return;
        if (!first)
            out += ", ";
        out += yaml_quote(t);
        first = false;
    };
    for (char c : csv) {
        if (c == ',')
            flush();
        else
            token += c;
    }
    flush();
    out += "]";
    return out;
}

} // namespace

std::string Today() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_val{};
    localtime_r(&t, &tm_val);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_val);
    return buf;
}

bool ProjectMarkdown(const std::string& path,
                     const std::string& project,
                     const std::vector<Article>& articles,
                     const NoteLookup& note_for) {
    return write_file(path, "project '" + project + "' Markdown", [&](std::ostream& file) {
        file << "# " << project << "\n\n";
        file << articles.size() << " article(s)\n\n---\n\n";
        for (const auto& article : articles) {
            std::string note = note_for(article.link);
            file << "## " << article.title << "\n\n";
            file << "**Authors:** " << article.authors << "  \n";
            file << "**Date:** " << format_date(article.date) << "  \n";
            file << "**Link:** " << article.link << "  \n\n";
            file << article.abstract << "\n\n";
            if (!note.empty()) {
                file << "> **Note:** " << note << "\n\n";
            }
            file << "---\n\n";
        }
    });
}

bool ProjectText(const std::string& path,
                 const std::string& project,
                 const std::vector<Article>& articles,
                 const NoteLookup& note_for) {
    return write_file(path, "project '" + project + "' plain text", [&](std::ostream& file) {
        file << project << "\n" << std::string(project.size(), '=') << "\n\n";
        file << articles.size() << " article(s)\n\n";
        for (size_t i = 0; i < articles.size(); ++i) {
            const auto& article = articles[i];
            std::string note = note_for(article.link);
            file << "[" << (i + 1) << "] " << article.title << "\n";
            file << "    Authors: " << article.authors << "\n";
            file << "    Date: " << format_date(article.date) << "\n";
            file << "    Link: " << article.link << "\n";
            if (!note.empty())
                file << "    Note: " << note << "\n";
            file << "\n";
        }
    });
}

bool ProjectJSON(const std::string& path,
                 const std::string& project,
                 const std::vector<Article>& articles,
                 const NoteLookup& note_for) {
    return write_file(path, "project '" + project + "' JSON", [&](std::ostream& file) {
        nlohmann::json j;
        j["name"] = project;
        j["articles"] = nlohmann::json::array();
        for (const auto& a : articles) {
            auto ts =
                std::chrono::duration_cast<std::chrono::seconds>(a.date.time_since_epoch()).count();
            nlohmann::json entry;
            entry["link"] = a.link;
            entry["title"] = a.title;
            entry["authors"] = a.authors;
            entry["abstract"] = a.abstract;
            entry["date"] = ts;
            entry["note"] = note_for(a.link);
            j["articles"].push_back(std::move(entry));
        }
        // Invalid UTF-8 from a feed becomes U+FFFD rather than an exception.
        file << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    });
}

bool DailyDigestMarkdown(const std::string& path, const std::vector<Article>& articles) {
    return write_file(path,
                      "daily digest (" + std::to_string(articles.size()) + " articles)",
                      [&](std::ostream& f) {
                          f << "# arXiv Daily Digest — " << Today() << "\n\n";
                          if (articles.empty()) {
                              f << "_No articles fetched today._\n";
                          } else {
                              for (const auto& a : articles) {
                                  f << "## " << a.title << "\n";
                                  f << "**Authors:** " << a.authors << "  \n";
                                  f << "**Link:** <" << a.link << ">  \n";
                                  if (!a.abstract.empty())
                                      f << "\n" << a.abstract << "\n";
                                  f << "\n---\n\n";
                              }
                          }
                      });
}

bool DailyDigestYAML(const std::string& path,
                     const std::vector<std::pair<float, Article>>& scored) {
    // yaml-cpp does the quoting: titles and abstracts are full of LaTeX
    // backslashes, which hand-rolled double-quoted scalars turned into
    // invalid escapes ("\\m") or silently into control characters ("\\t").
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "date" << YAML::Value << YAML::DoubleQuoted << Today();
    out << YAML::Key << "articles" << YAML::Value << YAML::BeginSeq;
    for (std::size_t i = 0; i < scored.size(); ++i) {
        const auto& a = scored[i].second;
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << YAML::DoubleQuoted << a.id();
        out << YAML::Key << "title" << YAML::Value << YAML::DoubleQuoted << a.title;
        out << YAML::Key << "authors" << YAML::Value << YAML::DoubleQuoted << a.authors;
        out << YAML::Key << "link" << YAML::Value << YAML::DoubleQuoted << a.link;
        out << YAML::Key << "category" << YAML::Value << YAML::DoubleQuoted << a.category;
        out << YAML::Key << "score" << YAML::Value << fmt::format("{:.3f}", scored[i].first);
        if (!a.abstract.empty())
            out << YAML::Key << "abstract" << YAML::Value << YAML::DoubleQuoted << a.abstract;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq << YAML::EndMap;

    return write_file(path,
                      "daily digest YAML (" + std::to_string(scored.size()) + " articles)",
                      [&](std::ostream& f) { f << out.c_str() << "\n"; });
}

bool BibTeXFile(const std::string& path,
                const std::vector<Article>& articles,
                const std::function<std::string(const Article&)>& entry_for) {
    return write_file(
        path, std::to_string(articles.size()) + " article(s) BibTeX", [&](std::ostream& file) {
            for (const auto& a : articles)
                file << entry_for(a) << "\n";
        });
}

std::string SelectedDigest(Fetcher& fetcher,
                           const std::vector<Article>& articles,
                           const std::string& download_dir,
                           const Progress& progress) {
    const std::string date_dir = Today();
    namespace fs = std::filesystem;
    fs::path digest_dir = fs::path(download_dir) / date_dir;
    std::error_code ec;
    fs::create_directories(digest_dir, ec);
    if (ec) {
        spdlog::error(
            "[Export]: Cannot create digest dir {}: {}", digest_dir.string(), ec.message());
        return "";
    }

    // Write the markdown digest.
    fs::path md_path = digest_dir / "digest.md";
    std::ostringstream f;
    f << "# arXiv Selected Digest — " << date_dir << "\n\n";
    f << articles.size() << " selected article(s)\n\n---\n\n";
    for (const auto& a : articles) {
        f << "## " << a.title << "\n\n";
        f << "**Authors:** " << a.authors << "  \n";
        f << "**Link:** <" << a.link << ">  \n";
        f << "**PDF:** [" << a.file_stem() << ".pdf](" << a.file_stem() << ".pdf)  \n";
        if (!a.category.empty())
            f << "**Category:** " << a.category << "  \n";
        if (!a.abstract.empty())
            f << "\n" << a.abstract << "\n";
        f << "\n---\n\n";
    }
    if (!WriteFileAtomically(md_path, f.str())) {
        spdlog::error("[Export]: Cannot write digest file {}", md_path.string());
        return "";
    }

    // Download each PDF next to the digest.
    int ok = 0;
    for (const auto& a : articles) {
        if (fetcher.DownloadPaper(a.id(), (digest_dir / (a.file_stem() + ".pdf")).string()))
            ++ok;
        if (progress)
            progress();
    }
    spdlog::info("[Export]: Selected digest written to {} ({}/{} PDFs downloaded)",
                 digest_dir.string(),
                 ok,
                 articles.size());

    return digest_dir.string();
}

std::string PackArchive(const std::string& dir) {
    namespace fs = std::filesystem;
    fs::path dir_path(dir);
    fs::path archive = dir_path.parent_path() / (dir_path.filename().string() + ".tar.gz");

    // Pack the digest directory into a .tar.gz alongside it.
    if (Process::RunAndWait({"tar",
                             "-czf",
                             archive.string(),
                             "-C",
                             dir_path.parent_path().string(),
                             dir_path.filename().string()}) != 0) {
        spdlog::error("[Export]: tar failed for digest archive {}", archive.string());
        return "";
    }
    spdlog::info("[Export]: Digest archive written to {}", archive.string());
    return archive.string();
}

namespace {

// A new paper note: YAML frontmatter, abstract, PDF link or embed, and an
// empty Reading Notes section for the user.
std::string obsidian_note(const Article& a,
                          const std::string& aid,
                          const std::string& stem,
                          const std::string& date_str,
                          bool download_pdfs) {
    std::ostringstream nf;
    nf << "---\n";
    nf << "title: " << yaml_quote(a.title) << "\n";
    nf << "authors: " << yaml_quote(a.authors) << "\n";
    nf << "arxiv_id: " << yaml_quote(aid) << "\n";
    nf << "url: " << yaml_quote(a.link) << "\n";
    if (!a.category.empty()) {
        nf << "tags: " << yaml_inline_list(a.category) << "\n";
        nf << "category: " << yaml_quote(a.category) << "\n";
    }
    nf << "imported: " << date_str << "\n";
    nf << "related-projects: []\n";
    nf << "---\n\n";
    nf << "# " << a.title << "\n\n";
    nf << "**Authors:** " << a.authors << "\n\n";
    nf << "**Link:** [" << a.link << "](" << a.link << ")\n\n";
    if (download_pdfs) {
        // Obsidian PDF embed: ![[file.pdf]] renders inline when colocated.
        nf << "![[" << stem << ".pdf]]\n\n";
    } else {
        nf << "[PDF on arXiv](https://arxiv.org/pdf/" << aid << ")\n\n";
    }
    if (!a.abstract.empty()) {
        nf << "## Abstract\n\n" << a.abstract << "\n";
    }
    nf << "\n## Reading Notes\n\n"
       << "**Relevance:**\n\n"
       << "**Key ideas:**\n\n"
       << "**Open questions:**\n";
    return nf.str();
}

// "- [[stem|title]]" for the index; "|" and "]]" would end the wikilink.
std::string index_entry(const Article& a) {
    std::string alias = a.title;
    std::replace(alias.begin(), alias.end(), '|', '/');
    for (auto pos = alias.find("]]"); pos != std::string::npos; pos = alias.find("]]"))
        alias.replace(pos, 2, "] ]");
    return "- [[" + a.file_stem() + "|" + alias + "]]\n";
}

} // namespace

std::string ObsidianNotes(Fetcher& fetcher,
                          const std::vector<Article>& articles,
                          const std::string& vault,
                          bool download_pdfs,
                          const Progress& progress) {
    namespace fs = std::filesystem;
    const std::string date_str = Today();
    fs::path note_dir = fs::path(vault) / "arxiv-tui" / date_str;
    std::error_code ec;
    fs::create_directories(note_dir, ec);
    if (ec) {
        spdlog::error("[Export]: Cannot create vault dir {}: {}", note_dir.string(), ec.message());
        return "";
    }

    // Per-paper notes with YAML frontmatter + abstract + a PDF link (or embed).
    // By default the PDF stays on arXiv: vaults are often git repos, and
    // committing every downloaded PDF bloats them for no benefit.
    int pdf_ok = 0;
    for (const auto& a : articles) {
        const std::string aid = a.id();
        const std::string stem = a.file_stem();
        fs::path note_path = note_dir / (stem + ".md");

        // An existing note is the user's: it holds their reading notes.
        std::error_code exists_error;
        if (fs::exists(note_path, exists_error)) {
            spdlog::info("[Export]: Keeping existing note {}", note_path.string());
        } else if (!WriteFileAtomically(note_path,
                                        obsidian_note(a, aid, stem, date_str, download_pdfs))) {
            spdlog::error("[Export]: Cannot write note {}", note_path.string());
            if (progress)
                progress();
            continue;
        }

        // Download the PDF next to the note. Fetcher writes relative to
        // its own base_path (the configured download_dir), which is *not*
        // the vault. Use an absolute path via the std::filesystem API
        // instead so the file lands in the vault directory.
        if (download_pdfs) {
            fs::path pdf_path = note_dir / (stem + ".pdf");
            if (fetcher.DownloadPaper(aid, pdf_path.string()))
                ++pdf_ok;
        }
        if (progress)
            progress();
    }

    // Index note that links to every paper imported today. A second export
    // on the same day adds to it rather than replacing the first batch.
    fs::path index_path = note_dir / ("digest-" + date_str + ".md");
    std::string index;
    {
        std::ifstream existing(index_path);
        index.assign(std::istreambuf_iterator<char>(existing), std::istreambuf_iterator<char>());
    }
    if (index.empty()) {
        std::ostringstream head;
        head << "---\n";
        head << "title: " << yaml_quote("arXiv digest " + date_str) << "\n";
        head << "tags: [arxiv-digest]\n";
        head << "date: " << date_str << "\n";
        head << "---\n\n";
        head << "# arXiv Digest — " << date_str << "\n\n";
        index = head.str();
    } else if (index.back() != '\n') {
        index += '\n';
    }
    for (const auto& a : articles)
        if (index.find("[[" + a.file_stem() + "|") == std::string::npos)
            index += index_entry(a);
    if (!WriteFileAtomically(index_path, index)) {
        spdlog::error("[Export]: Cannot write index note {}", index_path.string());
        return "";
    }

    if (download_pdfs)
        spdlog::info("[Export]: Obsidian digest written to {} ({}/{} PDFs)",
                     index_path.string(),
                     pdf_ok,
                     articles.size());
    else
        spdlog::info("[Export]: Obsidian digest written to {} ({} notes, PDFs linked on arXiv)",
                     index_path.string(),
                     articles.size());
    return index_path.string();
}

} // namespace Arxiv::Export
