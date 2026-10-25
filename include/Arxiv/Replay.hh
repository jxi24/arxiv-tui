// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace Arxiv {

class AppCore;

// ---------------------------------------------------------------------------
// ReplayRecorder
//
// Records every mutating AppCore action as a JSON-Lines (JSONL) entry. Each
// entry has at minimum:
//   {"ts":<epoch_ms>,"action":"<name>", ... action-specific fields ...}
//
// Construct with no argument for in-memory-only mode; pass a file path to
// also stream entries to disk in real time.
// ---------------------------------------------------------------------------
class ReplayRecorder {
  public:
    /// In-memory only mode.
    ReplayRecorder();

    /// Stream mode: writes each entry to `file_path` as it is recorded and
    /// also keeps a copy in memory for crash reports.
    explicit ReplayRecorder(const std::string& file_path);

    ~ReplayRecorder();

    // --- Record methods (one per AppCore mutating action) ---

    void RecordSetFilterIndex(int index);
    void RecordSetArticleIndex(int index);
    void RecordToggleBookmark(const std::string& article_link);
    void RecordDownloadArticle(const std::string& article_id);
    void RecordAddProject(const std::string& name);
    void RecordRemoveProject(const std::string& name);
    void RecordSetProjectParent(const std::string& project, const std::string& parent);
    void RecordLinkArticleToProject(const std::string& article_link, const std::string& project);
    void RecordUnlinkArticleFromProject(const std::string& article_link,
                                        const std::string& project);
    void RecordSetDateRange(const std::string& start, const std::string& end);
    void
    RecordSetSearchQuery(const std::string& query, bool title, bool authors, bool abstract_field);
    void RecordSetProjectNote(const std::string& project,
                              const std::string& article_link,
                              const std::string& note);
    void RecordRateArticle(const std::string& article_link, int rating);
    void RecordForceRetrain();
    void RecordExportProjectMarkdown(const std::string& project, const std::string& path);
    void RecordExportProjectText(const std::string& project, const std::string& path);
    void RecordExportProjectJSON(const std::string& project, const std::string& path);
    void RecordImportProjectJSON(const std::string& path);
    void RecordExportArticleBibTeX(const std::string& article_link, const std::string& path);
    void RecordExportArticlesBibTeX(const std::string& path);
    void RecordExportProjectBibTeX(const std::string& project, const std::string& path);
    void RecordToggleSelection(const std::string& article_link);
    void RecordExportSelectedDigest(const std::string& path);
    void RecordExportToObsidian(const std::string& path);
    void RecordExportDailyDigest(const std::string& path);
    void RecordToggleCategory(const std::string& category);
    void RecordSetActiveCategories(const std::vector<std::string>& categories);
    void RecordSaveKeywords(const std::vector<std::string>& keywords);

    /// Diagnostic event — does not correspond to a replayable action. Used
    /// for instrumentation (e.g. lifecycle, fetch progress). The replay
    /// player ignores it (unknown action -> skipped). Thread-safe.
    void RecordEvent(const std::string& name, const std::string& detail = "");

    // --- Accessors ---

    /// Returns all recorded entries as a JSONL string (one JSON object per
    /// line, each terminated with '\n').
    // The in-memory copy (for crash reports) keeps only the newest entries;
    // the replay file keeps everything.
    static constexpr int kMaxEntries = 2000;

    std::string GetJSONL() const;

    /// Number of entries recorded so far.
    std::size_t GetCount() const;

    /// The replay file being streamed to, or "" in in-memory mode. Fixed at
    /// construction, so the crash handler can read it without locking.
    const std::string& GetFilePath() const { return m_file_path; }

  private:
    std::deque<std::string> m_entries; // newest kMaxEntries
    std::ofstream m_file;
    std::string m_file_path;
    mutable std::mutex m_mutex;

    void Record(const std::string& json_line);
    static long long NowMs();
};

// ---------------------------------------------------------------------------
// ReplayPlayer
//
// Reads a JSONL replay log and dispatches each action to an AppCore instance
// without requiring any TUI or UI interaction.
// ---------------------------------------------------------------------------
class ReplayPlayer {
  public:
    struct Options {
        /// Run actions that read or write files, download or touch the
        /// keyword/ranker files (exports, import, downloads, save_keywords).
        /// Off by default: a replay file may come from someone else.
        bool file_actions = false;
    };

    struct Result {
        int total = 0;
        int replayed = 0;
        int skipped = 0;
        std::string error;
    };

    /// Replay from a JSONL string, or from a crash report (its Replay Log
    /// section is replayed).
    static Result FromString(const std::string& jsonl, AppCore& core);
    static Result FromString(const std::string& jsonl, AppCore& core, const Options& options);

    /// Replay from a JSONL file or a crash report file.
    static Result FromFile(const std::string& path, AppCore& core);
    static Result FromFile(const std::string& path, AppCore& core, const Options& options);

  private:
    static bool DispatchAction(const std::string& json_line,
                               AppCore& core,
                               const Options& options,
                               std::string& error_out);
};

} // namespace Arxiv
