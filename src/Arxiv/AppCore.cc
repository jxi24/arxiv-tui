#include "Arxiv/AppCore.hh"
#include "spdlog/spdlog.h"

#include <algorithm>

namespace Arxiv {

AppCore::AppCore(const Config& config,
                 std::unique_ptr<DatabaseManager> db,
                 std::unique_ptr<Fetcher> fetcher)
    : m_config(config)
    , m_topics(config.get_topics())
    , m_db(std::move(db))
    , m_fetcher(std::move(fetcher))
    , m_recommend_threshold(config.get_recommend_threshold()) {

    spdlog::info("ArxivAppCore Initialized");
    auto articles = m_fetcher->Fetch();
    for(const auto &article : articles) {
        m_db->AddArticle(article);
    }
    RefreshFilterOptions();
    FetchArticles();
    // Try to restore a previously saved model first
    if (!m_ranker.Load(m_ranker_path)) {
        // No saved model — fit vocabulary and train from ratings in the DB
        auto all_articles = m_db->GetRecent(-1);
        m_ranker.FitVocabulary(all_articles);
        auto rated = m_db->GetRatedArticles();
        if (!rated.empty()) {
            m_ranker.Train(rated);
            m_ranker.Save(m_ranker_path);
        }
    }
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
    } else if (m_filter_index == 5) {  // Recommended
        auto today_articles = m_db->GetRecent(1);
        if (m_ranker.IsTrained()) {
            // Score each article and keep those above the threshold
            std::vector<std::pair<float, Article>> scored;
            scored.reserve(today_articles.size());
            for (const auto &a : today_articles) {
                float score = m_ranker.Predict(a);
                if (score >= m_recommend_threshold) {
                    scored.emplace_back(score, a);
                }
            }
            // Sort by predicted score descending
            std::sort(scored.begin(), scored.end(),
                      [](const auto &lhs, const auto &rhs) { return lhs.first > rhs.first; });
            m_current_articles.clear();
            for (auto &[score, article] : scored) {
                m_current_articles.push_back(std::move(article));
            }
        } else {
            // Ranker not trained yet — show today's articles unranked
            m_current_articles = today_articles;
        }
    } else {  // Project (index >= 6)
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
    m_filter_options = {"All Articles", "Bookmarks", "Today", "Range", "Search", "Recommended"};
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

void AppCore::RateArticle(const std::string &article_link, int rating) {
    if (rating < 1 || rating > 5) return;
    m_db->SetRating(article_link, rating);
    spdlog::info("[AppCore]: Rated article {} with {}", article_link, rating);

    // Retrain the ranker with the updated ratings
    auto all_articles = m_db->GetRecent(-1);
    m_ranker.FitVocabulary(all_articles);
    auto rated = m_db->GetRatedArticles();
    m_ranker.Train(rated);
    m_ranker.Save(m_ranker_path);

    // Refresh the current view if on the Recommended filter
    if (m_filter_index == 5) {
        FetchArticles();
    } else {
        NotifyArticleUpdate();
    }
}

int AppCore::GetArticleRating(const std::string &article_link) const {
    return m_db->GetRating(article_link);
}

float AppCore::GetPredictedScore(const Article &article) const {
    return m_ranker.Predict(article);
}

void AppCore::SetRecommendThreshold(float threshold) {
    m_recommend_threshold = threshold;
    if (m_filter_index == 5) {
        FetchArticles();
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
