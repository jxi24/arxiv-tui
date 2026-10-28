// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARXIV_APP_CORE
#define ARXIV_APP_CORE

#include "Arxiv/Article.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/ProjectService.hh"
#include "Arxiv/RankingService.hh"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Arxiv {

class ReplayRecorder; // forward decl — included only by users that pass one

class AppCore {
  public:
    /// Project import files larger than this are refused unread.
    static constexpr std::uintmax_t kMaxImportBytes = ProjectService::kMaxImportBytes;

    // Whether the initial network fetch runs on the constructor thread (Sync,
    // the default — chosen so existing tests with mocked fetchers see the
    // fetched data immediately) or on a background thread (Async — used by
    // the production TUI so launch is instant).
    // Sync/Async: fetch from arXiv inline or on a background thread.
    // Offline: no network fetch and no fetch bookkeeping (last fetch date,
    // New Articles anchor) — for headless exports and replay, which must not
    // consume the interactive session's day-boundary crossing.
    // Headless: what --fetch (cron) uses — fetches and stores like Sync, but
    // the user has not seen anything, so the New Articles window is left
    // for the next interactive session.
    enum class FetchMode { Sync, Async, Offline, Headless };

    // Constructor with dependency injection. `recorder` is optional and used
    // for diagnostic instrumentation (lifecycle / fetch-progress events) so
    // the post-mortem replay log captures *why* the UI was blocked.
    explicit AppCore(const Config& config,
                     std::unique_ptr<DatabaseManager> db,
                     std::unique_ptr<Fetcher> fetcher,
                     FetchMode fetch_mode = FetchMode::Sync,
                     ReplayRecorder* recorder = nullptr);
    ~AppCore();

    enum class SearchMode { title, authors, abstract };

    enum class FilterView {
        All = 0,
        Bookmarks = 1,
        Today = 2,
        Range = 3,
        Search = 4,
        Recommended = 5,
        FollowedAuthors = 6,
        NewArticles = 7,
        // The mirror image of NewArticles: same anchor, the replacement side of
        // the split. Between the two, every article the feed carried since the
        // last session is reachable from exactly one view.
        Updated = 8,
        Unread = 9,
        // The values above are also the filter indices of the fixed views.
        // Tag and Project are kinds, not indices: tags occupy indices
        // [kFirstTagIndex, m_project_start_index), projects the rest. Use
        // CurrentTag() / CurrentProject() for the selected name.
        Tag = 100,
        Project = 101,
    };
    static constexpr int kFirstTagIndex = 10; // right after Unread

    /// Name of the selected tag / project, or "" when another view is shown.
    std::string CurrentTag() const;
    std::string CurrentProject() const;

    // Article management
    void FetchArticles();
    void ToggleBookmark(const std::string& article_link);
    void MarkArticleRead(const std::string& article_link);
    bool DownloadArticle(const std::string& article_id);
    std::string GetBibtex(const Article& article);
    const std::vector<Article>& GetCurrentArticles() const;
    const std::vector<std::string>& GetCurrentTitles() const;

    // Rating and ranking
    void RateArticle(const std::string& article_link, int rating);
    // Rate all selected articles (or the focused article if no selection) with
    // the given score, triggering a single retrain check after all ratings are saved.
    void RateSelected(int rating);
    int GetArticleRating(const std::string& article_link) const;
    float GetPredictedScore(const Article& article) const;
    bool IsRankerTrained() const;
    bool IsTraining() const { return m_ranking.IsTraining(); }
    int PendingRatings() const { return m_ratings_since_train; }
    // Called from the UI refresh loop to re-fetch articles after background
    // training completes. Must be called on the main thread.
    // Returns true if the article list changed (the UI should redraw).
    bool TryRefetchIfNeeded();
    // Force a full cold-start retrain (new vocabulary, reset weights) regardless
    // of the pending-ratings counter.
    void ForceRetrain();

    /// Full path for an export file named `file_name` in the configured
    /// export directory (download_dir when export_dir is not set), which is
    /// created if needed (pass create = false to only compute the path, e.g.
    /// for display). The name is reduced to a single safe path component.
    std::string ExportPath(const std::string& file_name, bool create = true) const;
    void SetRecommendThreshold(float threshold);
    float GetRecommendThreshold() const { return m_recommend_threshold; }

    // Project management
    /// Returns the name as stored (trimmed), or "" if nothing was left.
    std::string AddProject(const std::string& project_name);
    void RemoveProject(const std::string& project_name);
    void SetProjectParent(const std::string& project_name, const std::string& parent);
    void LinkArticleToProject(const std::string& article_link, const std::string& project_name);
    void UnlinkArticleFromProject(const std::string& article_link, const std::string& project_name);
    std::vector<std::string> GetProjects() const;
    /// Rename (trimmed). The view follows a renamed project or tag. False if
    /// the name is empty or taken.
    bool RenameProject(const std::string& old_name, const std::string& new_name);
    bool RenameTag(const std::string& old_name, const std::string& new_name);
    /// Where `project` may move: "" (top level) and every project that is
    /// not `project` itself or one of its sub-projects.
    std::vector<std::string> ParentCandidates(const std::string& project) const;
    /// "" for a top-level project.
    std::string GetProjectParent(const std::string& project) const;
    std::vector<Article> GetArticlesForProject(const std::string& project_name) const;
    std::vector<std::string> GetProjectsForArticle(const std::string& article_link) const;

    // Tag management
    void AddTag(const std::string& name);
    void RemoveTag(const std::string& name);
    std::vector<std::string> GetTags() const;
    std::vector<std::string> GetTagsForArticle(const std::string& article_link) const;
    void LinkArticleToTag(const std::string& article_link, const std::string& tag_name);
    void UnlinkArticleFromTag(const std::string& article_link, const std::string& tag_name);
    std::string GetTagNameForFilter(int filter_index) const;

    /// How many of the target articles (selection, else the focused one)
    /// carry `tag`.
    enum class Coverage { None, Some, All };
    Coverage TagCoverage(const std::string& tag) const;
    /// Remove `tag` from the targets if all of them carry it, otherwise add
    /// it to all of them (creating the tag if new).
    void ToggleTagOnTargets(const std::string& tag);

    // Project notes
    void SetProjectNote(const std::string& project_name,
                        const std::string& article_link,
                        const std::string& note);
    std::string GetProjectNote(const std::string& project_name,
                               const std::string& article_link) const;

    // Export/import
    bool ExportProjectMarkdown(const std::string& project_name,
                               const std::string& output_path) const;
    bool ExportProjectText(const std::string& project_name, const std::string& output_path) const;
    bool ExportProjectJSON(const std::string& project_name, const std::string& output_path) const;
    bool ImportProjectJSON(const std::string& input_path);

    // BibTeX export
    bool ExportArticleBibTeX(const Article& article, const std::string& output_path) const;
    bool ExportArticlesBibTeX(const std::vector<Article>& articles,
                              const std::string& output_path) const;
    bool ExportProjectBibTeX(const std::string& project_name, const std::string& output_path);

    // Daily-digest export
    bool ExportDailyDigest(const std::string& output_path) const;
    bool ExportDailyDigestYAML(const std::string& output_path) const;

    // Fuzzy search: returns articles where title, authors, or abstract
    // has a similarity score >= threshold (0-100) against the query.
    std::vector<Article> FuzzySearchArticles(const std::string& query, int threshold = 80) const;

    // Returns the actual project name for a filter index >= 6 (accounting for indented
    // sub-projects)
    std::string GetProjectNameForFilter(int index) const;

    // Filter management
    const std::vector<std::string>& GetFilterOptions() const;
    void SetFilterIndex(int index);
    void SetFilterIndex(FilterView view);
    int GetFilterIndex() const;
    FilterView GetFilterView() const;
    // How many papers the filter entry at `index` holds (Bookmarks, Unread,
    // each tag and project), or nothing for views without a stored count.
    // Recounted after a change, not while a fetch is writing.
    std::optional<int> GetFilterCount(int index) const;
    // "Tags" / "Projects" for the first entry of that section, else "".
    std::string GetFilterSection(int index) const;

    // State management
    void SetArticleIndex(int index);
    int GetArticleIndex() const;
    bool IsArticleBookmarked(const std::string& article_link) const;

    // Callbacks for UI updates
    using ArticleUpdateCallback = std::function<void()>;
    void SetArticleUpdateCallback(ArticleUpdateCallback callback);
    void SetProjectUpdateCallback(ArticleUpdateCallback callback);

    // Date range methods
    void SetDateRange(const std::string& start_date, const std::string& end_date);
    void ClearDateRange();
    bool HasDateRange() const { return m_date_range.active; }
    std::pair<std::string, std::string> GetDateRange() const {
        return {m_date_range.start, m_date_range.end};
    }

    // Search methods
    void SetSearchQuery(const std::string& query,
                        bool search_title = true,
                        bool search_authors = true,
                        bool search_abstract = true);
    void ClearSearch();
    bool HasSearchQuery() const { return m_search.active; }
    std::string GetSearchQuery() const { return m_search.query; }

    // Author subscriptions
    void FollowAuthor(const std::string& author_name);
    void UnfollowAuthor(const std::string& author_name);
    std::vector<std::string> GetFollowedAuthors() const;
    std::vector<Article> GetArticlesForFollowedAuthors() const;

    // Background auto-refresh
    void StartAutoRefresh();
    // Test hook: refresh on a custom interval instead of auto_refresh_minutes.
    void StartAutoRefresh(std::chrono::milliseconds interval);
    void StopAutoRefresh();
    /// One network refresh on a background thread (the GUI's Refresh). False
    /// if a fetch is already running. The list updates through
    /// TryRefetchIfNeeded, like the auto-refresh.
    bool StartRefresh();
    void WaitForRefresh();
    bool IsAutoRefreshing() const;
    int GetAutoRefreshMinutes() const;

    // Initial network fetch state (see FetchMode at top of class).
    // While an Async fetch is in flight, IsFetching() is true so the UI can
    // render a "fetching..." indicator. WaitForInitialFetch() joins the
    // background thread (no-op in Sync mode).
    bool IsFetching() const { return m_fetching.load(); }
    // True once the most recent network fetch (initial or auto-refresh)
    // completed and was stored.
    bool LastFetchSucceeded() const { return m_last_fetch_ok.load(); }
    /// Why the last fetch (initial or auto-refresh) failed; empty once one
    /// succeeds. Thread-safe.
    std::string LastFetchError() const {
        std::lock_guard<std::mutex> lock(m_fetch_error_mutex);
        return m_fetch_error;
    }
    void WaitForInitialFetch();

    // Category (arxiv tag) filter applied across every view. The set is
    // initialised to all configured topics in the constructor — toggling a
    // category off hides it from every list. Each setter triggers
    // FetchArticles so the UI updates live.
    const std::vector<std::string>& GetTopics() const { return m_topics; }
    const std::set<std::string>& GetActiveCategories() const { return m_active_categories; }
    bool IsCategoryActive(const std::string& cat) const {
        return m_active_categories.count(cat) > 0;
    }
    void ToggleCategory(const std::string& cat);
    void SetActiveCategories(const std::set<std::string>& cats);

    // Per-session article selection. Used by ExportSelectedDigest to build
    // a curated markdown digest + PDF bundle. Selections live in memory
    // only — they don't persist across restarts (use bookmarks for that).
    void ToggleSelection(const std::string& link);
    void ClearSelections();

    // Returns the links that should be opened in the browser: the full
    // selection if any articles are selected, otherwise the focused article.
    std::vector<std::string> GetLinksToOpen() const;
    bool IsSelected(const std::string& link) const { return m_selected_links.count(link) > 0; }
    std::size_t GetSelectionCount() const { return m_selected_links.size(); }

    // Delete the focused article, or all selected articles if a selection is active.
    // Snapshots each article's full state (rating, project memberships, tags) into the
    // undo ring buffer before deletion. Clears the selection and refreshes the list.
    void DeleteCurrentOrSelected();

    // Undo support — ring buffer of delete snapshots.
    bool CanUndo() const;
    void UndoLastDelete();
    std::size_t GetUndoCapacity() const { return m_undo_capacity; }
    // Changing capacity resets the buffer (existing history is discarded).
    void SetUndoCapacity(std::size_t capacity);

    // Set the bookmark state on all selected articles (or the focused article if
    // no selection is active).
    void BookmarkSelected(bool bookmarked);

    // Link all selected articles (or the focused article) to the given project.
    void AddSelectedToProject(const std::string& project_name);

    // Write a markdown digest covering every selected article to
    //     <download_dir>/<YYYY-MM-DD>/digest.md
    // and download each selected article's PDF into the same directory.
    // Returns the digest directory's path on success, empty on failure.
    std::string ExportSelectedDigest();

    // Same as ExportSelectedDigest but also packs the output directory into
    //     <download_dir>/<YYYY-MM-DD>.tar.gz
    // and returns the archive path on success, empty on failure.
    std::string ExportSelectedDigestArchive();

    // Export the current selection into the configured Obsidian vault. Each
    // article becomes a Markdown note with YAML frontmatter at
    //     <vault>/arxiv-tui/<YYYY-MM-DD>/<arxiv_id>.md
    // its PDF is downloaded beside it, and an index note links them via
    // [[wikilinks]]. Returns the index note's path on success or empty
    // string if the vault isn't configured / the write fails.
    std::string ExportSelectedToObsidian();

    // ── Background tasks ────────────────────────────────────────────────────
    // Exports that write many files or download PDFs run on a worker thread so
    // the UI stays responsive; the UI polls GetBackgroundTaskStatus() to show
    // progress. One task runs at a time: Start* returns false if a task is
    // already running or there is nothing to do. The selection and settings
    // are captured when the task starts.
    struct BackgroundTaskStatus {
        bool running{false};
        std::string label;   // e.g. "Exporting to Obsidian"
        std::size_t done{0}; // items finished
        std::size_t total{0};
        std::string result;  // output path once the task has succeeded
        std::string error;   // set if the task failed
        std::string message; // one-line summary for the UI once finished
        std::chrono::steady_clock::time_point finished_at{};
    };
    bool StartExportSelectedToObsidian();
    bool StartExportSelectedDigest();
    bool StartExportSelectedDigestArchive();
    bool StartDownloadArticle(const std::string& article_link);
    BackgroundTaskStatus GetBackgroundTaskStatus() const;
    bool IsBackgroundTaskRunning() const { return GetBackgroundTaskStatus().running; }
    void WaitForBackgroundTask();

    // Live config access (used by settings dialog)
    const Config& GetConfig() const { return m_config; }
    // Apply edited settings (from the Settings dialog) to the running core.
    // Topics take effect on the next start.
    void ApplyConfig(const Config& config);

    // Keyword management (cold-start ranking)
    void ReloadKeywords();
    bool SaveKeywords(const std::vector<std::string>& keywords);
    std::vector<std::string> GetKeywords() const;

  private:
    // Full state captured for a single deleted article, sufficient to restore it.
    struct DeletedArticleSnapshot {
        Article article;
        int rating{0};
        // Each project the article belonged to, paired with its note in that project.
        std::vector<std::pair<std::string, std::string>> project_notes;
        std::vector<std::string> tags;
    };

    // One undo step covers all articles deleted in a single DeleteCurrentOrSelected call.
    using UndoEntry = std::vector<DeletedArticleSnapshot>;

    // Push one step onto the ring buffer; overwrites the oldest entry when full.
    void PushUndo(UndoEntry entry);
    // Pop and return the most-recently-pushed entry, or nullopt if empty.
    std::optional<UndoEntry> PopUndo();

    // Ring buffer storage.
    std::vector<UndoEntry> m_undo_buffer;
    std::size_t m_undo_write{0}; // next write slot
    std::size_t m_undo_count{0}; // live entries (≤ m_undo_capacity)
    std::size_t m_undo_capacity{10};

    Config m_config;
    std::vector<std::string> m_topics;
    std::unique_ptr<DatabaseManager> m_db;
    std::unique_ptr<Fetcher> m_fetcher;
    ProjectService m_projects; // project data; AppCore refreshes the views
    RankingService m_ranking;  // model, training thread, keywords
    std::atomic<bool> m_needs_refetch{false};
    int m_ratings_since_train{0};
    int m_retrain_interval{5};
    float m_recommend_threshold{3.5f};

    // Snapshot training data and spawn a background thread.
    // warm_start=true: keep existing vocab and weights as starting point.
    // warm_start=false: refit vocabulary and reset weights (full retrain).
    void SpawnTrainingThread(bool warm_start);
    void RefreshFromNetwork();
    // Replay/diagnostic event; a no-op without a recorder.
    void Record(const std::string& name, const std::string& detail = "") const;
    // The selection if any, otherwise the focused article (empty if none).
    std::vector<std::string> TargetLinks() const;
    // An article by link: the current list first, then the DB.
    std::optional<Article> FindArticle(const std::string& link) const;
    // The selected articles (in link order), resolved via FindArticle.
    std::vector<Article> SelectedArticles() const;
    bool MatchesActiveCategory(const std::string& categories) const;

    // Project notes as a lookup for Export::Project*.

    struct TaskOutcome {
        std::string result, error, message;
    };
    using TaskWork = std::function<TaskOutcome(const std::function<void()>& progress)>;
    bool StartTask(std::string label, std::size_t total, TaskWork work);
    bool StartDigestTask(bool archive);
    mutable std::mutex m_task_mutex;
    BackgroundTaskStatus m_task_status;      // guarded by m_task_mutex
    std::vector<std::string> m_pending_read; // guarded by m_task_mutex
    std::thread m_task_thread;

    std::vector<Article> m_current_articles;
    std::vector<std::string> m_current_titles;
    std::vector<std::string> m_filter_options;
    std::vector<std::string> m_filter_tag_names;
    int m_project_start_index{kFirstTagIndex};
    // Filter-pane counts, recounted lazily after anything marks them stale.
    mutable DatabaseManager::FilterCounts m_filter_counts;
    mutable bool m_filter_counts_stale{true};
    // Actual project names parallel to filter_options[6+] (display may be indented for
    // sub-projects)
    std::vector<std::string> m_filter_project_names;

    int m_filter_index{static_cast<int>(FilterView::NewArticles)};
    int m_fetched_filter_index{-1}; // the view m_current_articles holds
    int m_article_index{0};

    ArticleUpdateCallback m_article_update_callback;
    ArticleUpdateCallback m_project_update_callback;

    // Active filter state. Bundling each (active, data...) tuple as a struct
    // makes it impossible to set the data without also flipping `active`,
    // and it pairs the related fields visually for readers of FetchArticles.
    struct DateRangeFilter {
        bool active = false;
        std::string start;
        std::string end;

        void set(const std::string& s, const std::string& e) {
            start = s;
            end = e;
            active = true;
        }
        void clear() {
            active = false;
            start.clear();
            end.clear();
        }
    };
    struct SearchFilter {
        bool active = false;
        std::string query;
        // Fields to search; any combination (the TUI picks one, the GUI all).
        bool title = true;
        bool authors = false;
        bool abstract = false;

        void set(const std::string& q, bool in_title, bool in_authors, bool in_abstract) {
            query = q;
            title = in_title;
            authors = in_authors;
            abstract = in_abstract;
            active = true;
        }
        void clear() { *this = SearchFilter{}; }
    };

    DateRangeFilter m_date_range;
    SearchFilter m_search;
    // Articles whose searched fields nearly match (the fallback when the
    // full-text search finds nothing).
    std::vector<Article> FuzzySearch(const SearchFilter& search, int threshold = 80) const;

    // Background auto-refresh
    std::thread m_refresh_thread;
    std::thread m_refresh_once_thread; // StartRefresh
    std::atomic<bool> m_refresh_running{false};
    std::condition_variable m_refresh_cv;
    std::mutex m_refresh_mutex;
    int m_auto_refresh_minutes{0};

    // Initial network fetch state.
    std::thread m_initial_fetch_thread;
    std::atomic<bool> m_fetching{false};
    std::atomic<bool> m_last_fetch_ok{false};
    mutable std::mutex m_fetch_error_mutex;
    std::string m_fetch_error; // guarded by m_fetch_error_mutex
    void SetFetchError(std::string error) {
        std::lock_guard<std::mutex> lock(m_fetch_error_mutex);
        m_fetch_error = std::move(error);
    }
    ReplayRecorder* m_recorder = nullptr;

    // Active arxiv categories — empty = no filter. Initialised in the
    // constructor to the full set of configured topics so the user starts
    // seeing everything.
    std::set<std::string> m_active_categories;

    // Article links the user has tagged for the next digest export.
    // Session-scoped only.
    std::set<std::string> m_selected_links;

    // New-articles tracking: UTC date ("YYYY-MM-DD") that seeds the NewArticles view.
    // Set from the previous session's last_fetch_date on construction.
    // Only a fallback now — see m_new_articles_anchor_ts.
    std::string m_new_articles_since_date;

    // New-articles window: articles stored at or after this Unix timestamp are
    // new to the user. It is the instant the previous session finished
    // fetching, so it covers every day missed since then plus the current one.
    // 0 means "no anchor recorded yet" (fresh database or first run after the
    // upgrade), in which case the date window above is used instead.
    std::int64_t m_new_articles_anchor_ts{0};

    void RefreshTitles();
    void RefreshFilterOptions();
    void NotifyArticleUpdate();
};

} // namespace Arxiv

#endif
