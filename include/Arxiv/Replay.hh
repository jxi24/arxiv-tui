// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Arxiv {

class AppCore;
class Config;

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
    void RecordClearSelection();
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

    /// A replay file larger than this when the recorder opens it is moved to
    /// "<file>.1" (replacing an older one) and a new file is started.
    static constexpr std::uintmax_t kMaxFileBytes = std::uintmax_t{5} << 20;

    std::string GetJSONL() const;

    /// Number of entries recorded so far.
    std::size_t GetCount() const;

    /// The replay file being streamed to, or "" in in-memory mode. Fixed at
    /// construction, so the crash handler can read it without locking.
    const std::string& GetFilePath() const { return m_file_path; }

  private:
    std::deque<std::string> m_entries; // newest kMaxEntries
    int m_fd = -1;                     // the replay file: mode 0600, close-on-exec
    std::string m_file_path;
    mutable std::mutex m_mutex;

    void Record(const std::string& json_line);
    static long long NowMs();
};

// ---------------------------------------------------------------------------
// ReplayLog
//
// What --replay reads, whichever file it was given: a replay.jsonl log, or a
// crash report (CrashHandler), whose first line is a JSON header and whose
// "Replay Log" section is the same JSONL. Both parse to the entries to
// replay; a report also gives what crashed.
// ---------------------------------------------------------------------------
struct CrashInfo {
    std::string version; // arxiv-tui version that crashed ("" in old reports)
    std::string signal;  // "SIGSEGV", ...
    int signum = 0;
    std::string time;       // UTC, ISO 8601
    long long pid = 0;      // 0 in old reports
    std::string replay_log; // the replay file the report copied, if any
};

struct ReplayLog {
    std::optional<CrashInfo> crash;   // set when the text is a crash report
    std::vector<std::string> entries; // one JSON object per entry

    static ReplayLog Parse(const std::string& text);
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
        std::optional<CrashInfo> crash; // when replaying a crash report
    };

    /// The one replay loop: every other entry point parses into a ReplayLog
    /// and comes here.
    static Result Run(const ReplayLog& log, AppCore& core, const Options& options);

    /// Replay from a JSONL string, or from a crash report (its Replay Log
    /// section is replayed).
    static Result FromString(const std::string& jsonl, AppCore& core);
    static Result FromString(const std::string& jsonl, AppCore& core, const Options& options);

    /// Replay from a JSONL file or a crash report file.
    static Result FromFile(const std::string& path, AppCore& core);
    static Result FromFile(const std::string& path, AppCore& core, const Options& options);

    /// What --replay does: replay `path` (a log or a crash report) on an
    /// in-memory copy of config's database, with the ranker and keywords
    /// copied to a scratch directory that is removed afterwards. Nothing the
    /// user owns is changed; file actions run only inside the scratch copy
    /// when options.file_actions is set.
    static Result RunSandboxed(const Config& config, const std::string& path);
    static Result
    RunSandboxed(const Config& config, const std::string& path, const Options& options);

  private:
    static bool DispatchAction(const std::string& json_line,
                               AppCore& core,
                               const Options& options,
                               std::string& error_out);
};

} // namespace Arxiv
