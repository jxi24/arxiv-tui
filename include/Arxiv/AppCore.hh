#ifndef ARXIV_APP_CORE
#define ARXIV_APP_CORE

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/Article.hh"

namespace Arxiv {

class AppCore {
public:
    // Constructor with dependency injection
    explicit AppCore(const Config &config,
                    std::unique_ptr<DatabaseManager> db,
                    std::unique_ptr<Fetcher> fetcher);
    
    enum class SearchMode {
        title,
        authors,
        abstract
    };
    
    // Article management
    void FetchArticles();
    void ToggleBookmark(const std::string& article_link);
    bool DownloadArticle(const std::string& article_id);
    std::vector<Article> GetCurrentArticles() const;
    std::vector<std::string> GetCurrentTitles() const;
    std::vector<std::string> &GetCurrentTitles();
    
    // Project management
    void AddProject(const std::string& project_name);
    void RemoveProject(const std::string& project_name);
    void LinkArticleToProject(const std::string& article_link, const std::string& project_name);
    void UnlinkArticleFromProject(const std::string& article_link, const std::string& project_name);
    std::vector<std::string> GetProjects() const;
    std::vector<Article> GetArticlesForProject(const std::string& project_name) const;
    std::vector<std::string> GetProjectsForArticle(const std::string& article_link) const;
    
    // Filter management
    std::vector<std::string> GetFilterOptions() const;
    std::vector<std::string> &GetFilterOptions();
    void SetFilterIndex(int index);
    int GetFilterIndex() const;
    int &GetFilterIndex();
    
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
    bool HasDateRange() const { return has_date_range; }
    std::pair<std::string, std::string> GetDateRange() const { return {start_date, end_date}; }

    // Search methods
    void SetSearchQuery(const std::string& query, bool search_title = true, 
                       bool search_authors = true, bool search_abstract = true);
    void ClearSearch();
    bool HasSearchQuery() const { return has_search_query; }
    std::string GetSearchQuery() const { return search_query; }

private:
    Config m_config;
    std::vector<std::string> m_topics;
    std::unique_ptr<DatabaseManager> m_db;
    std::unique_ptr<Fetcher> m_fetcher;
    
    std::vector<Article> m_current_articles;
    std::vector<std::string> m_current_titles;
    std::vector<std::string> m_filter_options;
    
    int m_filter_index{0};
    int m_article_index{0};
    
    ArticleUpdateCallback m_article_update_callback;
    ArticleUpdateCallback m_project_update_callback;
    
    // Date range members
    bool has_date_range = false;
    std::string start_date;
    std::string end_date;
    
    // Search members
    bool has_search_query = false;
    std::string search_query;
    SearchMode search_mode = SearchMode::title;
    
    void RefreshTitles();
    void RefreshFilterOptions();
    void NotifyArticleUpdate();
};

} // namespace Arxiv

#endif 
