#ifndef ARXIV_APP_CORE
#define ARXIV_APP_CORE

#include <condition_variable>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>

#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Article.hh"
#include "Arxiv/Ranker.hh"

namespace Arxiv {

class AppCore {
public:
    // Constructor with dependency injection
    explicit AppCore(const Config &config,
                    std::unique_ptr<DatabaseManager> db,
                    std::unique_ptr<Fetcher> fetcher);
    ~AppCore();
    
    enum class SearchMode {
        title,
        authors,
        abstract
    };

    enum class FilterView {
        All             = 0,
        Bookmarks       = 1,
        Today           = 2,
        Range           = 3,
        Search          = 4,
        Recommended     = 5,
        FollowedAuthors = 6,
        NewArticles     = 7,
        Project         = 8,  // sentinel: any index >= 8 is a project
    };
    
    // Article management
    void FetchArticles();
    void ToggleBookmark(const std::string& article_link);
    bool DownloadArticle(const std::string& article_id);
    std::vector<Article> GetCurrentArticles() const;
    std::vector<std::string> GetCurrentTitles() const;
    std::vector<std::string> &GetCurrentTitles();

    // Rating and ranking
    void RateArticle(const std::string& article_link, int rating);
    int GetArticleRating(const std::string& article_link) const;
    float GetPredictedScore(const Article& article) const;
    bool IsRankerTrained() const;
    bool IsTraining() const { return m_training.load(); }
    int  PendingRatings() const { return m_ratings_since_train; }
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

    // Project notes
    void SetProjectNote(const std::string& project_name, const std::string& article_link, const std::string& note);
    std::string GetProjectNote(const std::string& project_name, const std::string& article_link) const;

    // Export/import
    bool ExportProjectMarkdown(const std::string& project_name, const std::string& output_path) const;
    bool ExportProjectText(const std::string& project_name, const std::string& output_path) const;
    bool ExportProjectJSON(const std::string& project_name, const std::string& output_path) const;
    bool ImportProjectJSON(const std::string& input_path);

    // BibTeX export
    bool ExportArticleBibTeX(const Article& article, const std::string& output_path) const;
    bool ExportArticlesBibTeX(const std::vector<Article>& articles, const std::string& output_path) const;
    bool ExportProjectBibTeX(const std::string& project_name, const std::string& output_path) const;

    // Daily-digest export
    bool ExportDailyDigest(const std::string& output_path) const;
    bool ExportDailyDigestYAML(const std::string& output_path) const;

    // Fuzzy search: returns articles where title, authors, or abstract
    // has a similarity score >= threshold (0-100) against the query.
    std::vector<Article> FuzzySearchArticles(const std::string& query, int threshold = 80) const;

    // Returns the actual project name for a filter index >= 6 (accounting for indented sub-projects)
    std::string GetProjectNameForFilter(int index) const;
    
    // Filter management
    std::vector<std::string> GetFilterOptions() const;
    std::vector<std::string> &GetFilterOptions();
    void SetFilterIndex(int index);
    void SetFilterIndex(FilterView view);
    int GetFilterIndex() const;
    int &GetFilterIndex();
    FilterView GetFilterView() const;
    
    // State management
    void SetArticleIndex(int index);
    int GetArticleIndex() const;
    int &GetArticleIndex();
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
    void SetSearchQuery(const std::string& query, bool search_title = true,
                       bool search_authors = true, bool search_abstract = true);
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
    int  GetAutoRefreshMinutes() const;

    // Keyword management (cold-start ranking)
    void ReloadKeywords();
    bool SaveKeywords(const std::vector<std::string>& keywords);
    std::vector<std::string> GetKeywords() const;

private:
    Config m_config;
    std::vector<std::string> m_topics;
    std::unique_ptr<DatabaseManager> m_db;
    std::unique_ptr<Fetcher> m_fetcher;
    Ranker m_ranker;
    mutable std::mutex m_ranker_mutex;
    std::thread m_train_thread;
    std::atomic<bool> m_training{false};
    std::atomic<bool> m_needs_refetch{false};
    int  m_ratings_since_train{0};
    int  m_retrain_interval{5};
    float m_recommend_threshold{3.5f};
    std::string m_ranker_path{"ranker.bin"};

    // Snapshot training data and spawn a background thread.
    // warm_start=true: keep existing vocab and weights as starting point.
    // warm_start=false: refit vocabulary and reset weights (full retrain).
    void SpawnTrainingThread(bool warm_start);
    
    std::vector<Article> m_current_articles;
    std::vector<std::string> m_current_titles;
    std::vector<std::string> m_filter_options;
    // Actual project names parallel to filter_options[6+] (display may be indented for sub-projects)
    std::vector<std::string> m_filter_project_names;
    
    int m_filter_index{static_cast<int>(FilterView::NewArticles)};
    int m_article_index{0};
    
    ArticleUpdateCallback m_article_update_callback;
    ArticleUpdateCallback m_project_update_callback;
    
    // Active filter state. Bundling each (active, data...) tuple as a struct
    // makes it impossible to set the data without also flipping `active`,
    // and it pairs the related fields visually for readers of FetchArticles.
    struct DateRangeFilter {
        bool        active = false;
        std::string start;
        std::string end;

        void set(const std::string& s, const std::string& e) {
            start = s; end = e; active = true;
        }
        void clear() { active = false; start.clear(); end.clear(); }
    };
    struct SearchFilter {
        bool        active = false;
        std::string query;
        SearchMode  mode   = SearchMode::title;

        void set(const std::string& q, SearchMode m) {
            query = q; mode = m; active = true;
        }
        void clear() { active = false; query.clear(); mode = SearchMode::title; }
    };

    DateRangeFilter m_date_range;
    SearchFilter    m_search;

    // Background auto-refresh
    std::thread              m_refresh_thread;
    std::atomic<bool>        m_refresh_running{false};
    std::condition_variable  m_refresh_cv;
    std::mutex               m_refresh_mutex;
    int                      m_auto_refresh_minutes{0};

    // Keyword cold-start
    std::vector<std::string> m_keywords;

    // New-articles tracking: UTC date ("YYYY-MM-DD") that seeds the NewArticles view.
    // Set from the previous session's last_fetch_date on construction.
    std::string m_new_articles_since_date;

    void RefreshTitles();
    void RefreshFilterOptions();
    void NotifyArticleUpdate();
};

} // namespace Arxiv

#endif 
