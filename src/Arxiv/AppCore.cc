#include "Arxiv/AppCore.hh"
#include "spdlog/spdlog.h"
#include "fmt/format.h"

#include <algorithm>
#include <chrono>

namespace Arxiv {

AppCore::AppCore(const Config& config,
                 std::unique_ptr<DatabaseManager> db,
                 std::unique_ptr<Fetcher> fetcher)
    : m_config(config)
    , m_topics(config.get_topics())
    , m_db(std::move(db))
    , m_fetcher(std::move(fetcher)) {
    
    spdlog::info("ArxivAppCore Initialized");
    auto articles = m_fetcher->Fetch();
    for(const auto &article : articles) {
        m_db->AddArticle(article);
    }
    RefreshFilterOptions();
    FetchArticles();
}

void AppCore::FetchArticles() {
    m_current_articles.clear();
    
    if (m_filter_index == 0) {  // All Articles
        m_current_articles = m_db->GetRecent(-1);
    } else if (m_filter_index == 1) {  // Bookmarks
        m_current_articles = m_db->ListBookmarked();
    } else if (m_filter_index == 2) {  // Today
        m_current_articles = m_db->GetRecent(1);
    } else if (m_filter_index == 3) {  // Range
        if (has_date_range) {
            m_current_articles = m_db->GetArticlesForDateRange(start_date, end_date);
        } else {
            m_current_articles = m_db->GetRecent(-1);
        }
    } else if (m_filter_index == 4) {  // Search
        if (has_search_query) {
            bool search_title = (search_mode == SearchMode::title);
            bool search_authors = (search_mode == SearchMode::authors);
            bool search_abstract = (search_mode == SearchMode::abstract);
            m_current_articles = m_db->SearchArticles(search_query, search_title, 
                                                    search_authors, search_abstract);
        } else {
            m_current_articles = m_db->GetRecent(-1);
        }
    } else {  // Project
        std::string project_name = m_filter_options[static_cast<size_t>(m_filter_index)];
        m_current_articles = GetArticlesForProject(project_name);
    }
    
    RefreshTitles();
    m_article_index = 0;
    NotifyArticleUpdate();
}

void AppCore::ToggleBookmark(const std::string& article_link) {
    auto it = std::find_if(m_current_articles.begin(), m_current_articles.end(),
                          [&](const Article& a) { return a.link == article_link; });
    
    if(it != m_current_articles.end()) {
        it->bookmarked = !it->bookmarked;
        m_db->ToggleBookmark(article_link, it->bookmarked);
        RefreshTitles();
        NotifyArticleUpdate();
    }
}

bool AppCore::DownloadArticle(const std::string &article_id) {
    return m_fetcher->DownloadPaper(article_id, article_id + ".pdf");
}

std::vector<Article> AppCore::GetCurrentArticles() const {
    return m_current_articles;
}

std::vector<std::string> AppCore::GetCurrentTitles() const {
    return m_current_titles;
}

std::vector<std::string> &AppCore::GetCurrentTitles() {
    return m_current_titles;
}

void AppCore::AddProject(const std::string& project_name) {
    if(!project_name.empty()) {
        m_db->AddProject(project_name);
        RefreshFilterOptions();
        if(m_project_update_callback) {
            m_project_update_callback();
        }
        NotifyArticleUpdate();
    }
}

void AppCore::RemoveProject(const std::string& project_name) {
    m_db->RemoveProject(project_name);
    RefreshFilterOptions();
    if(m_project_update_callback) {
        m_project_update_callback();
    }
    NotifyArticleUpdate();
}

void AppCore::LinkArticleToProject(const std::string& article_link, const std::string& project_name) {
    m_db->LinkArticleToProject(article_link, project_name);
    NotifyArticleUpdate();
}

void AppCore::UnlinkArticleFromProject(const std::string& article_link, const std::string& project_name) {
    m_db->UnlinkArticleFromProject(article_link, project_name);
    NotifyArticleUpdate();
}

std::vector<std::string> AppCore::GetProjects() const {
    return m_db->GetProjects();
}

std::vector<Article> AppCore::GetArticlesForProject(const std::string& project_name) const {
    return m_db->GetArticlesForProject(project_name);
}

std::vector<std::string> AppCore::GetFilterOptions() const {
    return m_filter_options;
}

std::vector<std::string> &AppCore::GetFilterOptions() {
    return m_filter_options;
}

void AppCore::SetFilterIndex(int index) {
    if(index != m_filter_index) {
        m_filter_index = index;
        FetchArticles();
    }
}

int AppCore::GetFilterIndex() const {
    return m_filter_index;
}

int &AppCore::GetFilterIndex() {
    return m_filter_index;
}

void AppCore::SetArticleIndex(int index) {
    if(index != m_article_index) {
        m_article_index = index;
        NotifyArticleUpdate();
    }
}

int AppCore::GetArticleIndex() const {
    return m_article_index;
}

int &AppCore::GetArticleIndex() {
    return m_article_index;
}

bool AppCore::IsArticleBookmarked(const std::string& article_link) const {
    auto it = std::find_if(m_current_articles.begin(), m_current_articles.end(),
                          [&](const Article& a) { return a.link == article_link; });
    return it != m_current_articles.end() && it->bookmarked;
}

void AppCore::SetArticleUpdateCallback(ArticleUpdateCallback callback) {
    m_article_update_callback = std::move(callback);
}

void AppCore::SetProjectUpdateCallback(ArticleUpdateCallback callback) {
    m_project_update_callback = std::move(callback);
}

void AppCore::RefreshTitles() {
    m_current_titles.clear();
    for(const auto& article : m_current_articles) {
        std::string display_title = article.title;
        if(article.bookmarked) {
            display_title = "⭐ " + display_title;
        }
        m_current_titles.push_back(display_title);
    }
}

void AppCore::RefreshFilterOptions() {
    m_filter_options = {"All Articles", "Bookmarks", "Today", "Range", "Search"};
    for(const auto& project : GetProjects()) {
        m_filter_options.push_back(project);
    }
}

void AppCore::SetDateRange(const std::string& start, const std::string& end) {
    start_date = start;
    end_date = end;
    has_date_range = true;
    FetchArticles();
}

void AppCore::ClearDateRange() {
    has_date_range = false;
    start_date.clear();
    end_date.clear();
    FetchArticles();
}

void AppCore::SetSearchQuery(const std::string& query, bool _search_title, 
                             bool _search_authors, bool _search_abstract) {
    search_query = query;
    if (_search_title) search_mode = SearchMode::title;
    else if (_search_authors) search_mode = SearchMode::authors;
    else if (_search_abstract) search_mode = SearchMode::abstract;
    has_search_query = true;
    FetchArticles();
}

void AppCore::ClearSearch() {
    has_search_query = false;
    search_query.clear();
    search_mode = SearchMode::title;
    FetchArticles();
}

void AppCore::NotifyArticleUpdate() {
    if(m_article_update_callback) {
        m_article_update_callback();
    }
}

std::vector<std::string> AppCore::GetProjectsForArticle(const std::string& article_link) const {
    return m_db->GetProjectsForArticle(article_link);
}

} // namespace Arxiv 
