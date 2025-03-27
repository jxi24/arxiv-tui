#include "Arxiv/AppCore.hh"
#include "spdlog/spdlog.h"

#include <algorithm>

namespace Arxiv {

AppCore::AppCore(const std::vector<std::string>& topics,
                 std::unique_ptr<DatabaseManager> db,
                 std::unique_ptr<Fetcher> fetcher)
    : m_topics(topics)
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
    spdlog::debug("[AppCore]: Fetching articles for filter_index {}", m_filter_index);
    m_article_index = 0;
    std::vector<Article> new_articles;
    
    if(m_filter_index == 1) {
        new_articles = m_db->ListBookmarked();
    } else if(m_filter_index == 0) {
        new_articles = m_db->GetRecent(-1);
    } else if(m_filter_index == 2) {
        new_articles = m_db->GetRecent(1);
    } else if(m_filter_index >= 3) {
        new_articles = m_db->GetArticlesForProject(m_filter_options[static_cast<size_t>(m_filter_index)]);
    }

    spdlog::debug("[AppCore]: Found {} articles", new_articles.size());
    m_current_articles = std::move(new_articles);
    RefreshTitles();
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
    m_filter_options = {"All Articles", "Bookmarks", "Today"};
    for(const auto& project : GetProjects()) {
        m_filter_options.push_back(project);
    }
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
