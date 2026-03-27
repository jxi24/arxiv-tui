#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>

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
    void RecordUnlinkArticleFromProject(const std::string& article_link, const std::string& project);
    void RecordSetDateRange(const std::string& start, const std::string& end);
    void RecordSetSearchQuery(const std::string& query, bool title, bool authors, bool abstract_field);
    void RecordSetProjectNote(const std::string& project, const std::string& article_link, const std::string& note);
    void RecordRateArticle(const std::string& article_link, int rating);
    void RecordForceRetrain();
    void RecordExportProjectMarkdown(const std::string& project, const std::string& path);
    void RecordExportProjectText(const std::string& project, const std::string& path);
    void RecordExportProjectJSON(const std::string& project, const std::string& path);
    void RecordImportProjectJSON(const std::string& path);

    // --- Accessors ---

    /// Returns all recorded entries as a JSONL string (one JSON object per
    /// line, each terminated with '\n').
    std::string GetJSONL() const;

    /// Number of entries recorded so far.
    std::size_t GetCount() const;

private:
    std::vector<std::string> m_entries;
    std::ofstream            m_file;
    mutable std::mutex       m_mutex;

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
    struct Result {
        int         total    = 0;
        int         replayed = 0;
        int         skipped  = 0;
        std::string error;
    };

    /// Replay from a JSONL string.
    static Result FromString(const std::string& jsonl, AppCore& core);

    /// Replay from a JSONL file.
    static Result FromFile(const std::string& path, AppCore& core);

private:
    static bool DispatchAction(const std::string& json_line, AppCore& core, std::string& error_out);
};

} // namespace Arxiv
