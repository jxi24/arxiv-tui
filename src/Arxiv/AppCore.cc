#include "Arxiv/AppCore.hh"
#include "spdlog/spdlog.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <map>

namespace Arxiv {

AppCore::AppCore(const Config& config,
                 std::unique_ptr<DatabaseManager> db,
                 std::unique_ptr<Fetcher> fetcher)
    : m_config(config)
    , m_topics(config.get_topics())
    , m_db(std::move(db))
    , m_fetcher(std::move(fetcher))
    , m_retrain_interval(config.get_retrain_interval())
    , m_recommend_threshold(config.get_recommend_threshold()) {

    spdlog::info("ArxivAppCore Initialized");
    auto articles = m_fetcher->Fetch();
    for(const auto &article : articles) {
        m_db->AddArticle(article);
    }
    RefreshFilterOptions();
    FetchArticles();
    // Try to restore a previously saved model; fall back to training if absent
    if (!m_ranker.Load(m_ranker_path)) {
        auto all_articles = m_db->GetRecent(-1);
        auto rated = m_db->GetRatedArticles();
        if (!rated.empty()) {
            m_ranker.FitVocabulary(all_articles);
            m_ranker.Train(rated);
            m_ranker.Save(m_ranker_path);
        }
    }
}

AppCore::~AppCore() {
    // Ensure the background training thread has finished before destruction.
    if (m_train_thread.joinable()) {
        m_train_thread.join();
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
    } else if (m_filter_index == 6) {  // Followed Authors
        m_current_articles = GetArticlesForFollowedAuthors();
    } else {  // Project (index >= 7)
        std::string project_name = GetProjectNameForFilter(m_filter_index);
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
    // Trim leading/trailing whitespace; reject if result is empty.
    std::string trimmed = project_name;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    auto last = trimmed.find_last_not_of(" \t\n\r");
    if (last == std::string::npos) trimmed.clear();
    else trimmed.erase(last + 1);

    if (!trimmed.empty()) {
        m_db->AddProject(trimmed);
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
    // Clamp to the valid range so callers cannot reach an unhandled branch.
    if (!m_filter_options.empty()) {
        int max_idx = static_cast<int>(m_filter_options.size()) - 1;
        index = std::max(0, std::min(index, max_idx));
    } else {
        index = 0;
    }
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
    m_filter_options = {"All Articles", "Bookmarks", "Today", "Range", "Search", "Recommended", "Followed Authors"};
    m_filter_project_names.clear();

    // Build hierarchy from projects table
    auto all_projects = m_db->GetProjects();

    std::vector<std::string> top_level;
    std::map<std::string, std::vector<std::string>> children;
    for (const auto& name : all_projects) {
        std::string parent = m_db->GetProjectParent(name);
        if (parent.empty()) {
            top_level.push_back(name);
        } else {
            children[parent].push_back(name);
        }
    }

    for (const auto& proj : top_level) {
        m_filter_options.push_back(proj);
        m_filter_project_names.push_back(proj);
        auto it = children.find(proj);
        if (it != children.end()) {
            for (const auto& child : it->second) {
                m_filter_options.push_back("  " + child);  // indented display
                m_filter_project_names.push_back(child);    // actual name
            }
        }
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

    ++m_ratings_since_train;
    spdlog::debug("[AppCore]: {} new rating(s) pending (threshold: {})",
                  m_ratings_since_train, m_retrain_interval);

    if (m_ratings_since_train >= m_retrain_interval) {
        m_ratings_since_train = 0;
        SpawnTrainingThread(/*warm_start=*/true);
    } else {
        // Not at threshold yet — just refresh the UI so the rating display
        // updates without triggering training.
        NotifyArticleUpdate();
    }
}

void AppCore::ForceRetrain() {
    spdlog::info("[AppCore]: Full retrain requested");
    m_ratings_since_train = 0;
    SpawnTrainingThread(/*warm_start=*/false);
}

void AppCore::SpawnTrainingThread(bool warm_start) {
    // Join any previously completed thread before spawning a new one.
    if (m_train_thread.joinable()) {
        m_train_thread.join();
    }

    // Snapshot training data on the main thread.
    auto all_articles = m_db->GetRecent(-1);
    auto rated        = m_db->GetRatedArticles();

    // For warm-start, copy the current ranker (vocab + weights) to the thread.
    // For cold-start, a default-constructed Ranker will be used.
    Ranker seed_ranker;
    if (warm_start) {
        std::lock_guard<std::mutex> lock(m_ranker_mutex);
        seed_ranker = m_ranker;
    }

    m_training = true;
    m_train_thread = std::thread([this, warm_start,
                                   all_articles = std::move(all_articles),
                                   rated        = std::move(rated),
                                   seed_ranker  = std::move(seed_ranker)]() mutable {
        if (!warm_start) {
            // Cold start: build fresh vocabulary then train from scratch.
            seed_ranker = Ranker{};
            seed_ranker.FitVocabulary(all_articles);
        }
        // warm_start=true keeps the existing vocab; only SGD continues.
        seed_ranker.Train(rated, warm_start);
        seed_ranker.Save(m_ranker_path);

        {
            std::lock_guard<std::mutex> lock(m_ranker_mutex);
            m_ranker = std::move(seed_ranker);
        }
        m_training      = false;
        m_needs_refetch = true;
        NotifyArticleUpdate();
    });
}

bool AppCore::IsRankerTrained() const {
    std::lock_guard<std::mutex> lock(m_ranker_mutex);
    return m_ranker.IsTrained();
}

void AppCore::TryRefetchIfNeeded() {
    if (m_needs_refetch.exchange(false)) {
        FetchArticles();
    }
}

int AppCore::GetArticleRating(const std::string &article_link) const {
    return m_db->GetRating(article_link);
}

float AppCore::GetPredictedScore(const Article &article) const {
    std::lock_guard<std::mutex> lock(m_ranker_mutex);
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

void AppCore::SetProjectParent(const std::string& project_name, const std::string& parent) {
    m_db->SetProjectParent(project_name, parent);
    RefreshFilterOptions();
    if (m_project_update_callback) m_project_update_callback();
    NotifyArticleUpdate();
}

void AppCore::SetProjectNote(const std::string& project_name, const std::string& article_link,
                             const std::string& note) {
    m_db->SetProjectNote(project_name, article_link, note);
    NotifyArticleUpdate();
}

std::string AppCore::GetProjectNote(const std::string& project_name,
                                    const std::string& article_link) const {
    return m_db->GetProjectNote(project_name, article_link);
}

std::string AppCore::GetProjectNameForFilter(int index) const {
    int proj_index = index - 7;  // offset by 7: 0-5 base filters, 6 = Followed Authors
    if (proj_index >= 0 && static_cast<size_t>(proj_index) < m_filter_project_names.size()) {
        return m_filter_project_names[static_cast<size_t>(proj_index)];
    }
    // Fallback: use raw filter option string
    if (static_cast<size_t>(index) < m_filter_options.size()) {
        std::string name = m_filter_options[static_cast<size_t>(index)];
        // Strip leading spaces (indented children)
        size_t start = name.find_first_not_of(' ');
        return start == std::string::npos ? name : name.substr(start);
    }
    return "";
}

// ─── Export helpers ───────────────────────────────────────────────────────────

static std::string format_date_str(const std::chrono::system_clock::time_point& tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = {};
    localtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

bool AppCore::ExportProjectMarkdown(const std::string& project_name,
                                    const std::string& output_path) const {
    auto articles = m_db->GetArticlesForProject(project_name);
    std::ofstream file(output_path);
    if (!file.is_open()) return false;

    file << "# " << project_name << "\n\n";
    file << articles.size() << " article(s)\n\n---\n\n";

    for (const auto& article : articles) {
        std::string note = m_db->GetProjectNote(project_name, article.link);
        file << "## " << article.title << "\n\n";
        file << "**Authors:** " << article.authors << "  \n";
        file << "**Date:** " << format_date_str(article.date) << "  \n";
        file << "**Link:** " << article.link << "  \n\n";
        file << article.abstract << "\n\n";
        if (!note.empty()) {
            file << "> **Note:** " << note << "\n\n";
        }
        file << "---\n\n";
    }
    spdlog::info("[AppCore]: Exported project '{}' to Markdown: {}", project_name, output_path);
    return true;
}

bool AppCore::ExportProjectText(const std::string& project_name,
                                const std::string& output_path) const {
    auto articles = m_db->GetArticlesForProject(project_name);
    std::ofstream file(output_path);
    if (!file.is_open()) return false;

    file << project_name << "\n" << std::string(project_name.size(), '=') << "\n\n";
    file << articles.size() << " article(s)\n\n";

    for (size_t i = 0; i < articles.size(); ++i) {
        const auto& article = articles[i];
        std::string note = m_db->GetProjectNote(project_name, article.link);
        file << "[" << (i + 1) << "] " << article.title << "\n";
        file << "    Authors: " << article.authors << "\n";
        file << "    Date: " << format_date_str(article.date) << "\n";
        file << "    Link: " << article.link << "\n";
        if (!note.empty()) file << "    Note: " << note << "\n";
        file << "\n";
    }
    spdlog::info("[AppCore]: Exported project '{}' to plain text: {}", project_name, output_path);
    return true;
}

bool AppCore::ExportProjectJSON(const std::string& project_name,
                                const std::string& output_path) const {
    auto articles = m_db->GetArticlesForProject(project_name);
    std::ofstream file(output_path);
    if (!file.is_open()) return false;

    nlohmann::json j;
    j["name"] = project_name;
    j["articles"] = nlohmann::json::array();

    for (const auto& a : articles) {
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(
            a.date.time_since_epoch()).count();
        nlohmann::json entry;
        entry["link"]     = a.link;
        entry["title"]    = a.title;
        entry["authors"]  = a.authors;
        entry["abstract"] = a.abstract;
        entry["date"]     = ts;
        entry["note"]     = m_db->GetProjectNote(project_name, a.link);
        j["articles"].push_back(std::move(entry));
    }

    file << j.dump(2) << "\n";
    spdlog::info("[AppCore]: Exported project '{}' to JSON: {}", project_name, output_path);
    return true;
}

bool AppCore::ImportProjectJSON(const std::string& input_path) {
    std::ifstream file(input_path);
    if (!file.is_open()) {
        spdlog::error("[AppCore]: Cannot open import file: {}", input_path);
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        spdlog::error("[AppCore]: JSON parse error: {}", e.what());
        return false;
    }

    if (!j.contains("name") || !j["name"].is_string()) {
        spdlog::error("[AppCore]: Import JSON missing 'name' field");
        return false;
    }

    std::string project_name = j["name"].get<std::string>();
    m_db->AddProject(project_name);

    if (j.contains("articles") && j["articles"].is_array()) {
        for (const auto& entry : j["articles"]) {
            std::string link = entry.value("link", "");
            if (link.empty()) continue;

            Article article;
            article.link      = link;
            article.title     = entry.value("title", "");
            article.authors   = entry.value("authors", "");
            article.abstract  = entry.value("abstract", "");
            article.bookmarked = false;
            int64_t ts = entry.value("date", int64_t{0});
            article.date = std::chrono::system_clock::from_time_t(static_cast<time_t>(ts));

            m_db->AddArticle(article);
            m_db->LinkArticleToProject(link, project_name);

            std::string note = entry.value("note", "");
            if (!note.empty()) {
                m_db->SetProjectNote(project_name, link, note);
            }
        }
    }

    RefreshFilterOptions();
    if (m_project_update_callback) m_project_update_callback();
    NotifyArticleUpdate();
    spdlog::info("[AppCore]: Imported project '{}' from JSON: {}", project_name, input_path);
    return true;
}

// ---------------------------------------------------------------------------
// BibTeX helpers
// ---------------------------------------------------------------------------

namespace {

/// Extract the last name from the first author in a comma-separated list.
/// e.g. "John Doe, Jane Smith" → "Doe"
static std::string first_author_last_name(const std::string& authors) {
    // Take the portion before the first comma (= first author)
    std::string first = authors.substr(0, authors.find(','));
    // Last word is the last name
    std::size_t space = first.rfind(' ');
    if (space == std::string::npos) return first;
    return first.substr(space + 1);
}

/// Build a fallback BibTeX entry from Article metadata.
static std::string build_fallback_bibtex(const Arxiv::Article& article) {
    // Extract arXiv ID from link (last path component)
    std::string arxiv_id = article.id();

    // Year from date
    auto tt = std::chrono::system_clock::to_time_t(article.date);
    std::tm tm = {};
    localtime_r(&tt, &tm);
    char year_buf[8];
    std::strftime(year_buf, sizeof(year_buf), "%Y", &tm);

    // Citation key: LastnameYear:arxivID
    std::string key = first_author_last_name(article.authors) + ":" + year_buf
                      + "_" + arxiv_id;
    // Replace non-alphanumeric (except :_.) with underscore
    for (char& c : key) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != ':' && c != '_' && c != '.') {
            c = '_';
        }
    }

    std::string bib;
    bib += "@article{" + key + ",\n";
    bib += "  author        = {" + article.authors + "},\n";
    bib += "  title         = {{" + article.title + "}},\n";
    bib += "  eprint        = {" + arxiv_id + "},\n";
    bib += "  archivePrefix = {arXiv},\n";
    if (!article.category.empty()) {
        bib += "  primaryClass  = {" + article.category + "},\n";
    }
    bib += "  year          = {" + std::string(year_buf) + "},\n";
    bib += "  url           = {" + article.link + "},\n";
    bib += "}\n";
    return bib;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BibTeX export — AppCore methods
// ---------------------------------------------------------------------------

/// Fetch BibTeX for one article: try InspireHEP via the fetcher; fall back
/// to constructing from article metadata.
static std::string get_bibtex(const Arxiv::Article& article, Arxiv::Fetcher* fetcher) {
    std::string arxiv_id = article.id();
    std::string bib;
    if (!arxiv_id.empty() && fetcher) {
        bib = fetcher->FetchBibTeX(arxiv_id);
    }
    if (bib.empty()) {
        bib = build_fallback_bibtex(article);
    }
    return bib;
}

bool AppCore::ExportArticleBibTeX(const Article& article,
                                  const std::string& output_path) const {
    std::ofstream file(output_path);
    if (!file.is_open()) return false;
    file << get_bibtex(article, m_fetcher.get()) << "\n";
    spdlog::info("[AppCore]: Exported article '{}' BibTeX to {}", article.title, output_path);
    return true;
}

bool AppCore::ExportArticlesBibTeX(const std::vector<Article>& articles,
                                   const std::string& output_path) const {
    std::ofstream file(output_path);
    if (!file.is_open()) return false;
    for (const auto& a : articles) {
        file << get_bibtex(a, m_fetcher.get()) << "\n";
    }
    spdlog::info("[AppCore]: Exported {} article(s) BibTeX to {}", articles.size(), output_path);
    return true;
}

bool AppCore::ExportProjectBibTeX(const std::string& project_name,
                                  const std::string& output_path) const {
    auto articles = m_db->GetArticlesForProject(project_name);
    std::ofstream file(output_path);
    if (!file.is_open()) return false;
    for (const auto& a : articles) {
        file << get_bibtex(a, m_fetcher.get()) << "\n";
    }
    spdlog::info("[AppCore]: Exported project '{}' BibTeX to {}", project_name, output_path);
    return true;
}

// ---------------------------------------------------------------------------
// Author subscriptions
// ---------------------------------------------------------------------------

void AppCore::FollowAuthor(const std::string& author_name) {
    m_db->FollowAuthor(author_name);
}

void AppCore::UnfollowAuthor(const std::string& author_name) {
    m_db->UnfollowAuthor(author_name);
}

std::vector<std::string> AppCore::GetFollowedAuthors() const {
    return m_db->GetFollowedAuthors();
}

std::vector<Article> AppCore::GetArticlesForFollowedAuthors() const {
    auto followed = m_db->GetFollowedAuthors();
    if (followed.empty()) return {};

    auto all = m_db->GetRecent(-1);
    std::vector<Article> result;
    for (const auto& a : all) {
        for (const auto& author : followed) {
            if (a.authors.find(author) != std::string::npos) {
                result.push_back(a);
                break;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Daily-digest export
// ---------------------------------------------------------------------------

static std::string today_string() {
    auto now   = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_val{};
    localtime_r(&t, &tm_val);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_val);
    return buf;
}

bool AppCore::ExportDailyDigest(const std::string& output_path) const {
    auto articles = m_db->GetRecent(1); // last 24 hours
    std::ofstream f(output_path);
    if (!f.is_open()) return false;

    f << "# arXiv Daily Digest — " << today_string() << "\n\n";
    if (articles.empty()) {
        f << "_No articles fetched today._\n";
    } else {
        for (const auto& a : articles) {
            f << "## " << a.title << "\n";
            f << "**Authors:** " << a.authors << "  \n";
            f << "**Link:** <" << a.link << ">  \n";
            if (!a.abstract.empty())
                f << "\n" << a.abstract << "\n";
            f << "\n---\n\n";
        }
    }
    spdlog::info("[AppCore]: Exported daily digest ({} articles) to {}", articles.size(), output_path);
    return true;
}

bool AppCore::ExportDailyDigestYAML(const std::string& output_path) const {
    auto articles = m_db->GetRecent(1);
    std::ofstream f(output_path);
    if (!f.is_open()) return false;

    f << "date: \"" << today_string() << "\"\narticles:\n";
    for (const auto& a : articles) {
        auto escape_yaml = [](const std::string& s) {
            std::string out;
            out.reserve(s.size());
            for (char c : s) {
                if (c == '"') out += "\\\"";
                else out += c;
            }
            return out;
        };
        f << "  - title: \"" << escape_yaml(a.title) << "\"\n";
        f << "    authors: \"" << escape_yaml(a.authors) << "\"\n";
        f << "    link: \"" << escape_yaml(a.link) << "\"\n";
        if (!a.abstract.empty())
            f << "    abstract: \"" << escape_yaml(a.abstract) << "\"\n";
    }
    spdlog::info("[AppCore]: Exported daily digest YAML ({} articles) to {}", articles.size(), output_path);
    return true;
}

// ---------------------------------------------------------------------------
// Keyword management
// ---------------------------------------------------------------------------

void AppCore::ReloadKeywords() {
    const std::string& path = m_config.get_keywords_file();
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::debug("[AppCore]: Keywords file '{}' not found — skipping", path);
        return;
    }

    std::vector<std::string> kws;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty())
            kws.push_back(line);
    }
    m_keywords = kws;
    {
        std::lock_guard<std::mutex> lock(m_ranker_mutex);
        m_ranker.FitKeywords(kws);
    }
    spdlog::info("[AppCore]: Loaded {} keyword(s) from '{}'", kws.size(), path);
}

bool AppCore::SaveKeywords(const std::vector<std::string>& keywords) {
    const std::string& path = m_config.get_keywords_file();
    if (path.empty()) return false;

    std::ofstream f(path);
    if (!f.is_open()) {
        spdlog::error("[AppCore]: Cannot write keywords file '{}'", path);
        return false;
    }
    for (const auto& kw : keywords)
        f << kw << "\n";

    m_keywords = keywords;
    {
        std::lock_guard<std::mutex> lock(m_ranker_mutex);
        m_ranker.FitKeywords(keywords);
    }
    spdlog::info("[AppCore]: Saved {} keyword(s) to '{}'", keywords.size(), path);
    return true;
}

std::vector<std::string> AppCore::GetKeywords() const {
    return m_keywords;
}

} // namespace Arxiv
