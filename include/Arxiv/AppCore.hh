// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARXIV_APP_CORE
#define ARXIV_APP_CORE

#include "Arxiv/Article.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Ranker.hh"

#include <atomic>
#include <condition_variable>
#include <cstddef>
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
    // Whether the initial network fetch runs on the constructor thread (Sync,
    // the default — chosen so existing tests with mocked fetchers see the
    // fetched data immediately) or on a background thread (Async — used by
    // the production TUI so launch is instant).
    enum class FetchMode { Sync, Async };

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
        Unread = 8,
        // Filter indices [TagBase, m_project_start_index) are tags.
        // Filter indices [m_project_start_index, ...) are projects.
        // GetFilterView() uses those runtime boundaries — TagBase and Project
        // are used only as return-value tags in switch statements.
        TagBase = 9,
        Project = 100, // sentinel returned by GetFilterView() for project indices
    };

    // Article management
    void FetchArticles();
    void ToggleBookmark(const std::string& article_link);
    void MarkArticleRead(const std::string& article_link);
    bool DownloadArticle(const std::string& article_id);
    std::string GetBibtex(const Article& article);
    std::vector<Article> GetCurrentArticles() const;
    std::vector<std::string> GetCurrentTitles() const;
    std::vector<std::string>& GetCurrentTitles();

    // Rating and ranking
    void RateArticle(const std::string& article_link, int rating);
    // Rate all selected articles (or the focused article if no selection) with
    // the given score, triggering a single retrain check after all ratings are saved.
    void RateSelected(int rating);
    int GetArticleRating(const std::string& article_link) const;
    float GetPredictedScore(const Article& article) const;
    bool IsRankerTrained() const;
    bool IsTraining() const { return m_training.load(); }
    int PendingRatings() const { return m_ratings_since_train; }
    // Called from the UI refresh loop to re-fetch articles after background
    // training completes. Must be called on the main thread.
    void TryRefetchIfNeeded();
    // Force a full cold-start retrain (new vocabulary, reset weights) regardless
    // of the pending-ratings counter.
    void ForceRetrain();
    void SetRecommendThreshold(float threshold);
    float GetRecommendThreshold() const { return m_recommend_threshold; }

    // Project management
    void AddProject(const std::string& project_name);
    void RemoveProject(const std::string& project_name);
    void SetProjectParent(const std::string& project_name, const std::string& parent);
    void LinkArticleToProject(const std::string& article_link, const std::string& project_name);
    void UnlinkArticleFromProject(const std::string& article_link, const std::string& project_name);
    std::vector<std::string> GetProjects() const;
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
    std::vector<std::string> GetFilterOptions() const;
    std::vector<std::string>& GetFilterOptions();
    void SetFilterIndex(int index);
    void SetFilterIndex(FilterView view);
    int GetFilterIndex() const;
    int& GetFilterIndex();
    FilterView GetFilterView() const;

    // State management
    void SetArticleIndex(int index);
    int GetArticleIndex() const;
    int& GetArticleIndex();
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
    void StopAutoRefresh();
    bool IsAutoRefreshing() const;
    int GetAutoRefreshMinutes() const;

    // Initial network fetch state (see FetchMode at top of class).
    // While an Async fetch is in flight, IsFetching() is true so the UI can
    // render a "fetching..." indicator. WaitForInitialFetch() joins the
    // background thread (no-op in Sync mode).
    bool IsFetching() const { return m_fetching.load(); }
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

    // Live config access (used by settings dialog)
    const Config& GetConfig() const { return m_config; }

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
    Ranker m_ranker;
    mutable std::mutex m_ranker_mutex;
    std::thread m_train_thread;
    std::atomic<bool> m_training{false};
    std::atomic<bool> m_needs_refetch{false};
    int m_ratings_since_train{0};
    int m_retrain_interval{5};
    float m_recommend_threshold{3.5f};
    std::string m_ranker_path{"ranker.bin"};

    // Snapshot training data and spawn a background thread.
    // warm_start=true: keep existing vocab and weights as starting point.
    // warm_start=false: refit vocabulary and reset weights (full retrain).
    void SpawnTrainingThread(bool warm_start);

    std::vector<Article> m_current_articles;
    std::vector<std::string> m_current_titles;
    std::vector<std::string> m_filter_options;
    std::vector<std::string> m_filter_tag_names;
    int m_project_start_index{static_cast<int>(FilterView::TagBase)};
    // Actual project names parallel to filter_options[6+] (display may be indented for
    // sub-projects)
    std::vector<std::string> m_filter_project_names;

    int m_filter_index{static_cast<int>(FilterView::NewArticles)};
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
        SearchMode mode = SearchMode::title;

        void set(const std::string& q, SearchMode m) {
            query = q;
            mode = m;
            active = true;
        }
        void clear() {
            active = false;
            query.clear();
            mode = SearchMode::title;
        }
    };

    DateRangeFilter m_date_range;
    SearchFilter m_search;

    // Background auto-refresh
    std::thread m_refresh_thread;
    std::atomic<bool> m_refresh_running{false};
    std::condition_variable m_refresh_cv;
    std::mutex m_refresh_mutex;
    int m_auto_refresh_minutes{0};

    // Initial network fetch state.
    std::thread m_initial_fetch_thread;
    std::atomic<bool> m_fetching{false};
    ReplayRecorder* m_recorder = nullptr;

    // Active arxiv categories — empty = no filter. Initialised in the
    // constructor to the full set of configured topics so the user starts
    // seeing everything.
    std::set<std::string> m_active_categories;

    // Article links the user has tagged for the next digest export.
    // Session-scoped only.
    std::set<std::string> m_selected_links;

    // Keyword cold-start
    std::vector<std::string> m_keywords;

    // New-articles tracking: UTC date ("YYYY-MM-DD") that seeds the NewArticles view.
    // Set from the previous session's last_fetch_date on construction.
    std::string m_new_articles_since_date;

    void RefreshTitles();
    void RefreshFilterOptions();
    void NotifyArticleUpdate();
    std::string ConstructBibtexFromArticle(const Article& article) const;
};

} // namespace Arxiv

#endif
