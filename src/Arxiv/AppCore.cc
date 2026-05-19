#include "Arxiv/AppCore.hh"
#include "Arxiv/FuzzyMatch.hh"
#include "Arxiv/Replay.hh"
#include "spdlog/spdlog.h"
#include <nlohmann/json.hpp>
#include "fmt/format.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string_view>

namespace Arxiv {

static std::string today_utc_string() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_val{};
    gmtime_r(&t, &tm_val);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_val);
    return buf;
}

AppCore::AppCore(const Config& config,
                 std::unique_ptr<DatabaseManager> db,
                 std::unique_ptr<Fetcher> fetcher,
                 FetchMode fetch_mode,
                 ReplayRecorder* recorder)
    : m_config(config)
    , m_topics(config.get_topics())
    , m_db(std::move(db))
    , m_fetcher(std::move(fetcher))
    , m_retrain_interval(config.get_retrain_interval())
    , m_recommend_threshold(config.get_recommend_threshold())
    , m_ranker_path(config.get_ranker_file())
    , m_auto_refresh_minutes(config.get_auto_refresh_minutes())
    , m_recorder(recorder) {

    spdlog::info("ArxivAppCore Initialized");
    if (m_recorder) m_recorder->RecordEvent("appcore/ctor_begin");

    // Show every configured topic by default; user toggles via the filter
    // dialog.
    m_active_categories.insert(m_topics.begin(), m_topics.end());

    const std::string today      = today_utc_string();
    const std::string prev_fetch = m_db->GetMetadata("last_fetch_date");
    std::string anchor           = m_db->GetMetadata("new_articles_anchor");

    // The "New Articles" anchor only advances on a real day-boundary
    // crossing. A same-day restart must NOT collapse the anchor to today —
    // doing so would push GetArticlesSince() past every existing article and
    // make the New Articles view appear empty.
    if (!prev_fetch.empty() && prev_fetch < today) {
        anchor = prev_fetch;
        m_db->SetMetadata("new_articles_anchor", anchor);
    }
    m_new_articles_since_date = anchor;
    m_db->SetMetadata("last_fetch_date", today);

    if (m_recorder) m_recorder->RecordEvent("appcore/metadata_loaded",
                                            "prev_fetch=" + prev_fetch + " anchor=" + anchor);

    // Show whatever is already in the local DB immediately.
    RefreshFilterOptions();
    FetchArticles();
    if (m_recorder) m_recorder->RecordEvent("appcore/initial_fetcharticles_done",
                                            "count=" + std::to_string(m_current_articles.size()));

    // Try to restore a previously saved model; fall back to training if absent.
    if (!m_ranker.Load(m_ranker_path)) {
        auto all_articles = m_db->GetRecent(-1);
        auto rated = m_db->GetRatedArticles();
        if (!rated.empty()) {
            m_ranker.FitVocabulary(all_articles);
            m_ranker.Train(rated);
            m_ranker.Save(m_ranker_path);
        }
    }
    if (m_recorder) m_recorder->RecordEvent("appcore/ranker_loaded",
                                            std::string("trained=") + (m_ranker.IsTrained() ? "1" : "0"));

    // Network fetch. In Async mode it runs on a background thread so the TUI
    // launches immediately; IsFetching() reports its state. In Sync mode
    // (the test default) the same logic runs inline so callers see fetched
    // data as soon as the constructor returns.
    //
    // The bg thread NEVER touches m_current_articles or the
    // article-update callback directly: it only sets m_needs_refetch so the
    // UI thread can refresh on its next tick via TryRefetchIfNeeded(). This
    // avoids racing with UI-thread reads and avoids invoking the callback
    // before App's SetupUI() has installed it.
    auto do_fetch = [this, prev_fetch, today, fetch_mode]() {
        if (m_recorder) m_recorder->RecordEvent("appcore/bg_fetch_begin",
                                                "mode=" + std::string(fetch_mode == FetchMode::Async ? "async" : "sync"));
        std::vector<Article> articles;
        if (!prev_fetch.empty() && prev_fetch < today) {
            articles = m_fetcher->FetchSince(prev_fetch);
        } else {
            articles = m_fetcher->Fetch();
        }
        if (m_recorder) m_recorder->RecordEvent("appcore/bg_fetch_returned",
                                                "n=" + std::to_string(articles.size()));

        // Wrap the bulk insert in a single transaction. Without this each
        // AddArticle is its own commit (ms-scale fsync each); the UI-thread
        // DB read could block for the entire window.
        if (m_recorder) m_recorder->RecordEvent("appcore/bg_db_insert_begin");
        m_db->AddArticles(articles);
        if (m_recorder) m_recorder->RecordEvent("appcore/bg_db_insert_end");

        if (fetch_mode == FetchMode::Sync) {
            // Sync callers expect m_current_articles to reflect the fetch
            // immediately after the constructor returns.
            FetchArticles();
        } else {
            // UI thread will pick this up via TryRefetchIfNeeded.
            m_needs_refetch.store(true);
        }
        m_fetching.store(false);
        if (m_recorder) m_recorder->RecordEvent("appcore/bg_fetch_done");
    };

    m_fetching.store(true);
    if (fetch_mode == FetchMode::Async) {
        if (m_recorder) m_recorder->RecordEvent("appcore/fetch_thread_spawning");
        m_initial_fetch_thread = std::thread(std::move(do_fetch));
    } else {
        do_fetch();
    }
    if (m_recorder) m_recorder->RecordEvent("appcore/ctor_end");
}

AppCore::~AppCore() {
    if (m_recorder) m_recorder->RecordEvent("appcore/dtor_begin",
                                            std::string("fetching=") + (m_fetching.load() ? "1" : "0"));
    StopAutoRefresh();
    WaitForInitialFetch();
    // Ensure the background training thread has finished before destruction.
    if (m_train_thread.joinable()) {
        m_train_thread.join();
    }
    if (m_recorder) m_recorder->RecordEvent("appcore/dtor_end");
}

void AppCore::WaitForInitialFetch() {
    if (m_initial_fetch_thread.joinable()) {
        m_initial_fetch_thread.join();
    }
}

void AppCore::ToggleCategory(const std::string& cat) {
    if (m_active_categories.count(cat)) m_active_categories.erase(cat);
    else                                m_active_categories.insert(cat);
    FetchArticles();
}

void AppCore::SetActiveCategories(const std::set<std::string>& cats) {
    m_active_categories = cats;
    FetchArticles();
}

void AppCore::FetchArticles() {
    if (m_recorder) m_recorder->RecordEvent("appcore/fetcharticles_begin",
                                            "view=" + std::to_string(static_cast<int>(GetFilterView())));
    m_current_articles.clear();

    switch (GetFilterView()) {
    case FilterView::All:
        m_current_articles = m_db->GetRecent(-1);
        break;
    case FilterView::Bookmarks:
        m_current_articles = m_db->ListBookmarked();
        break;
    case FilterView::Today:
        m_current_articles = m_db->GetRecent(1);
        break;
    case FilterView::Range:
        if (m_date_range.active) {
            m_current_articles = m_db->GetArticlesForDateRange(m_date_range.start, m_date_range.end);
        } else {
            m_current_articles = m_db->GetRecent(-1);
        }
        break;
    case FilterView::Search:
        if (m_search.active) {
            bool search_title    = (m_search.mode == SearchMode::title);
            bool search_authors  = (m_search.mode == SearchMode::authors);
            bool search_abstract = (m_search.mode == SearchMode::abstract);
            m_current_articles = m_db->SearchArticles(m_search.query, search_title,
                                                      search_authors, search_abstract);
        } else {
            m_current_articles = m_db->GetRecent(-1);
        }
        break;
    case FilterView::Recommended: {
        auto today_articles = m_db->GetRecent(1);
        if (m_ranker.IsTrained()) {
            std::vector<std::pair<float, Article>> scored;
            scored.reserve(today_articles.size());
            for (const auto &a : today_articles) {
                float score = m_ranker.Predict(a);
                if (score >= m_recommend_threshold)
                    scored.emplace_back(score, a);
            }
            std::sort(scored.begin(), scored.end(),
                      [](const auto &lhs, const auto &rhs) { return lhs.first > rhs.first; });
            for (auto &[score, article] : scored)
                m_current_articles.push_back(std::move(article));
        } else {
            m_current_articles = today_articles;
        }
        break;
    }
    case FilterView::FollowedAuthors:
        m_current_articles = GetArticlesForFollowedAuthors();
        break;
    case FilterView::NewArticles: {
        if (m_new_articles_since_date.empty()) {
            // No anchor: first-ever open. Show all articles in the DB.
            m_current_articles = m_db->GetRecent(-1);
        } else if (m_fetching.load()) {
            // Async network fetch still in progress. Today's articles aren't
            // in the DB yet, so the advanced-date query would return empty.
            // Fall back to showing the anchor-date articles so the view is
            // not blank while the user waits for the fetch to complete.
            m_current_articles = m_db->GetArticlesSince(m_new_articles_since_date);
        } else {
            // Fetch complete (or Sync mode). Show articles strictly after the
            // previous session's date by advancing the anchor by one day.
            std::tm tm{};
            tm.tm_year = std::stoi(m_new_articles_since_date.substr(0, 4)) - 1900;
            tm.tm_mon  = std::stoi(m_new_articles_since_date.substr(5, 2)) - 1;
            tm.tm_mday = std::stoi(m_new_articles_since_date.substr(8, 2)) + 1;
            timegm(&tm); // normalise
            char buf[11];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
            auto advanced_articles = m_db->GetArticlesSince(buf);
            if (!advanced_articles.empty()) {
                m_current_articles = std::move(advanced_articles);
            } else {
                // Today's articles haven't arrived yet even though the fetch
                // finished (e.g. arXiv not yet published, or same-day restart
                // before new content). Show anchor-date articles as fallback.
                m_current_articles = m_db->GetArticlesSince(m_new_articles_since_date);
            }
        }
        // Drop replacements (later versions of older submissions) — the
        // New Articles view is meant for truly fresh papers only.
        m_current_articles.erase(
            std::remove_if(m_current_articles.begin(), m_current_articles.end(),
                [](const Article& a) { return a.is_replacement; }),
            m_current_articles.end());
        // If a trained ranker is available, surface the most relevant new
        // articles first. No threshold is applied — the user wants to see
        // *every* new paper, just in priority order. Predict is called
        // exactly once per article (cached side-vector) to keep the sort
        // O(N log N) on the comparisons.
        if (m_ranker.IsTrained() && m_current_articles.size() > 1) {
            std::vector<std::pair<float, Article>> scored;
            scored.reserve(m_current_articles.size());
            {
                std::lock_guard<std::mutex> lock(m_ranker_mutex);
                for (auto& a : m_current_articles) {
                    scored.emplace_back(m_ranker.Predict(a), std::move(a));
                }
            }
            std::sort(scored.begin(), scored.end(),
                      [](const auto& l, const auto& r) { return l.first > r.first; });
            m_current_articles.clear();
            for (auto& [s, a] : scored) m_current_articles.push_back(std::move(a));
        }
        break;
    }
    case FilterView::Project:
        m_current_articles = GetArticlesForProject(GetProjectNameForFilter(m_filter_index));
        break;
    }

    // Apply the global category filter on top of the per-view selection.
    // An article matches if any of its (comma-separated) categories is in
    // the active set. Semantics: ticked = visible. Articles with an empty
    // category field pass through unconditionally — keeps rows inserted
    // before the schema migration visible until a fresh fetch backfills them.
    const std::set<std::string> all_topics(m_topics.begin(), m_topics.end());
    if (m_active_categories != all_topics) {
        m_current_articles.erase(
            std::remove_if(m_current_articles.begin(), m_current_articles.end(),
                [this](const Article& a) {
                    if (a.category.empty()) return false;
                    for (const auto& cat : m_active_categories) {
                        if (a.category.find(cat) != std::string::npos) return false;
                    }
                    return true;
                }),
            m_current_articles.end());
    }

    RefreshTitles();
    m_article_index = 0;
    if (m_recorder) m_recorder->RecordEvent("appcore/fetcharticles_end",
                                            "count=" + std::to_string(m_current_articles.size()));
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

std::string AppCore::GetBibtex(const Article& article) {
    std::string arxiv_id = article.id();
    if(arxiv_id.empty()) return "";

    std::string bibtex = m_fetcher->FetchBibTeX(arxiv_id);
    if(!bibtex.empty()) {
        return bibtex;
    }

    return ConstructBibtexFromArticle(article);
}

std::string AppCore::ConstructBibtexFromArticle(const Article& article) const {
    std::string arxiv_id = article.id();
    if(arxiv_id.empty()) return "";

    // Extract first author's last name for the cite key
    std::string first_author_last;
    std::string authors_str = article.authors;
    size_t comma_pos = authors_str.find(',');
    std::string first_author = (comma_pos != std::string::npos)
        ? authors_str.substr(0, comma_pos) : authors_str;

    // Get last name (last word of the first author)
    size_t last_space = first_author.rfind(' ');
    if(last_space != std::string::npos) {
        first_author_last = first_author.substr(last_space + 1);
    } else {
        first_author_last = first_author;
    }

    // Extract year and month from article date
    auto time_t = std::chrono::system_clock::to_time_t(article.date);
    std::tm tm = *std::localtime(&time_t);
    std::string year = std::to_string(1900 + tm.tm_year);
    std::string month = std::to_string(1 + tm.tm_mon);

    // Extract primary class from category (first one)
    std::string primary_class = article.category;
    size_t cat_comma = primary_class.find(',');
    if(cat_comma != std::string::npos) {
        primary_class = primary_class.substr(0, cat_comma);
    }
    // Trim whitespace
    while(!primary_class.empty() && primary_class.back() == ' ') primary_class.pop_back();

    // Format authors for BibTeX (replace ", " with " and ")
    std::string bibtex_authors = article.authors;
    std::string search = ", ";
    std::string replace = " and ";
    size_t pos = 0;
    while((pos = bibtex_authors.find(search, pos)) != std::string::npos) {
        bibtex_authors.replace(pos, search.length(), replace);
        pos += replace.length();
    }

    return fmt::format(
        "@article{{{0}:{1},\n"
        "    author = \"{2}\",\n"
        "    title = \"{{{3}}}\",\n"
        "    eprint = \"{1}\",\n"
        "    archivePrefix = \"arXiv\",\n"
        "    primaryClass = \"{4}\",\n"
        "    year = \"{5}\",\n"
        "    month = \"{6}\"\n"
        "}}",
        first_author_last, arxiv_id, bibtex_authors, article.title,
        primary_class, year, month);
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

AppCore::FilterView AppCore::GetFilterView() const {
    if (m_filter_index >= static_cast<int>(FilterView::Project))
        return FilterView::Project;
    return static_cast<FilterView>(m_filter_index);
}

void AppCore::SetFilterIndex(FilterView view) {
    SetFilterIndex(static_cast<int>(view));
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
        if (m_selected_links.count(article.link)) {
            display_title = "[*] " + display_title;
        } else if (article.bookmarked) {
            display_title = "⭐ " + display_title;
        }
        m_current_titles.push_back(display_title);
    }
}

void AppCore::RefreshFilterOptions() {
    m_filter_options = {"All Articles", "Bookmarks", "Today", "Range", "Search", "Recommended", "Followed Authors", "New Articles"};
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
    m_date_range.set(start, end);
    FetchArticles();
}

void AppCore::ClearDateRange() {
    m_date_range.clear();
    FetchArticles();
}

void AppCore::SetSearchQuery(const std::string& query, bool _search_title,
                             bool _search_authors, bool _search_abstract) {
    SearchMode mode = SearchMode::title;
    if (_search_title)         mode = SearchMode::title;
    else if (_search_authors)  mode = SearchMode::authors;
    else if (_search_abstract) mode = SearchMode::abstract;
    m_search.set(query, mode);
    FetchArticles();
}

void AppCore::ClearSearch() {
    m_search.clear();
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
    // Don't query the DB while the background fetch is still writing —
    // SQLite would serialise the read against every pending insert and the
    // UI thread would block for the duration of the bulk insert.
    if (m_fetching.load()) {
        if (m_recorder) m_recorder->RecordEvent("appcore/try_refetch_skipped", "fetching");
        return;
    }
    if (m_needs_refetch.exchange(false)) {
        if (m_recorder) m_recorder->RecordEvent("appcore/try_refetch_executing");
        FetchArticles();   // FetchArticles also fires NotifyArticleUpdate
        if (m_recorder) m_recorder->RecordEvent("appcore/try_refetch_executed",
                                                "count=" + std::to_string(m_current_articles.size()));
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
    if (GetFilterView() == FilterView::Recommended) {
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
    int proj_index = index - static_cast<int>(FilterView::Project);  // 0-based into project list
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

// ---------------------------------------------------------------------------
// Export helper
//
// All Export* methods share the same skeleton: open the output file, bail
// out if that fails, write format-specific content, and log success. This
// helper isolates the boilerplate so each Export* body is just "fetch data,
// describe how to write it."
// ---------------------------------------------------------------------------
namespace {
bool write_export(const std::string& path,
                  std::string_view log_subject,
                  const std::function<void(std::ofstream&)>& writer) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    writer(file);
    spdlog::info("[AppCore]: Exported {} to {}", log_subject, path);
    return true;
}
} // namespace

bool AppCore::ExportProjectMarkdown(const std::string& project_name,
                                    const std::string& output_path) const {
    auto articles = m_db->GetArticlesForProject(project_name);
    return write_export(output_path,
                        "project '" + project_name + "' Markdown",
                        [&](std::ofstream& file) {
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
    });
}

bool AppCore::ExportProjectText(const std::string& project_name,
                                const std::string& output_path) const {
    auto articles = m_db->GetArticlesForProject(project_name);
    return write_export(output_path,
                        "project '" + project_name + "' plain text",
                        [&](std::ofstream& file) {
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
    });
}

bool AppCore::ExportProjectJSON(const std::string& project_name,
                                const std::string& output_path) const {
    auto articles = m_db->GetArticlesForProject(project_name);
    return write_export(output_path,
                        "project '" + project_name + "' JSON",
                        [&](std::ofstream& file) {
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
    });
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

bool AppCore::ExportArticlesBibTeX(const std::vector<Article>& articles,
                                   const std::string& output_path) const {
    return write_export(output_path,
                        std::to_string(articles.size()) + " article(s) BibTeX",
                        [&](std::ofstream& file) {
        for (const auto& a : articles) {
            file << get_bibtex(a, m_fetcher.get()) << "\n";
        }
    });
}

bool AppCore::ExportArticleBibTeX(const Article& article,
                                  const std::string& output_path) const {
    return ExportArticlesBibTeX({article}, output_path);
}

bool AppCore::ExportProjectBibTeX(const std::string& project_name,
                                  const std::string& output_path) const {
    return ExportArticlesBibTeX(m_db->GetArticlesForProject(project_name),
                                output_path);
}

// ---------------------------------------------------------------------------
// Fuzzy search
// ---------------------------------------------------------------------------

std::vector<Article> AppCore::FuzzySearchArticles(const std::string& query, int threshold) const {
    auto all = m_db->GetRecent(-1);
    std::vector<Article> results;
    for (const auto& a : all) {
        if (FuzzyMatch::MatchesText(query, a.title,    threshold) ||
            FuzzyMatch::MatchesText(query, a.authors,  threshold) ||
            FuzzyMatch::MatchesText(query, a.abstract, threshold)) {
            results.push_back(a);
        }
    }
    return results;
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
    return write_export(output_path,
                        "daily digest (" + std::to_string(articles.size()) + " articles)",
                        [&](std::ofstream& f) {
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
    });
}

bool AppCore::ExportDailyDigestYAML(const std::string& output_path) const {
    auto articles = m_db->GetRecent(1);
    return write_export(output_path,
                        "daily digest YAML (" + std::to_string(articles.size()) + " articles)",
                        [&](std::ofstream& f) {
        auto escape_yaml = [](const std::string& s) {
            std::string out;
            out.reserve(s.size());
            for (char c : s) {
                if (c == '"') out += "\\\"";
                else out += c;
            }
            return out;
        };
        f << "date: \"" << today_string() << "\"\narticles:\n";
        for (const auto& a : articles) {
            f << "  - title: \"" << escape_yaml(a.title) << "\"\n";
            f << "    authors: \"" << escape_yaml(a.authors) << "\"\n";
            f << "    link: \"" << escape_yaml(a.link) << "\"\n";
            if (!a.abstract.empty())
                f << "    abstract: \"" << escape_yaml(a.abstract) << "\"\n";
        }
    });
}

// ---------------------------------------------------------------------------
// Article selection + curated digest
// ---------------------------------------------------------------------------

void AppCore::ToggleSelection(const std::string& link) {
    if (m_selected_links.count(link)) m_selected_links.erase(link);
    else                              m_selected_links.insert(link);
    RefreshTitles();
    NotifyArticleUpdate();
}

void AppCore::ClearSelections() {
    m_selected_links.clear();
    RefreshTitles();
    NotifyArticleUpdate();
}

std::string AppCore::ExportSelectedDigest() {
    if (m_selected_links.empty()) {
        spdlog::warn("[AppCore]: ExportSelectedDigest called with no selections");
        return "";
    }

    // Resolve selected links to full Article rows. Fetching everything once
    // and filtering avoids N round-trips for typical selection sizes.
    auto all = m_db->GetRecent(-1);
    std::vector<Article> picked;
    picked.reserve(m_selected_links.size());
    for (const auto& a : all) {
        if (m_selected_links.count(a.link)) picked.push_back(a);
    }
    if (picked.empty()) {
        spdlog::warn("[AppCore]: ExportSelectedDigest: no matching articles in DB");
        return "";
    }

    const std::string date_dir = today_string();
    namespace fs = std::filesystem;
    fs::path digest_dir = fs::path(m_config.get_download_dir()) / date_dir;
    std::error_code ec;
    fs::create_directories(digest_dir, ec);
    if (ec) {
        spdlog::error("[AppCore]: Cannot create digest dir {}: {}", digest_dir.string(), ec.message());
        return "";
    }

    // Write the markdown digest.
    fs::path md_path = digest_dir / "digest.md";
    std::ofstream f(md_path);
    if (!f.is_open()) {
        spdlog::error("[AppCore]: Cannot open digest file {}", md_path.string());
        return "";
    }
    f << "# arXiv Selected Digest — " << date_dir << "\n\n";
    f << picked.size() << " selected article(s)\n\n---\n\n";
    for (const auto& a : picked) {
        f << "## " << a.title << "\n\n";
        f << "**Authors:** " << a.authors << "  \n";
        f << "**Link:** <" << a.link << ">  \n";
        f << "**PDF:** [" << a.id() << ".pdf](" << a.id() << ".pdf)  \n";
        if (!a.category.empty()) f << "**Category:** " << a.category << "  \n";
        if (!a.abstract.empty()) f << "\n" << a.abstract << "\n";
        f << "\n---\n\n";
    }
    f.close();

    // Download each PDF into the same directory. The Fetcher writes
    // relative to its base_path (= download_dir), so the relative
    // "<date_dir>/<id>.pdf" lands next to the digest.
    int ok = 0;
    for (const auto& a : picked) {
        std::string rel = date_dir + "/" + a.id() + ".pdf";
        if (m_fetcher->DownloadPaper(a.id(), rel)) ++ok;
    }
    spdlog::info("[AppCore]: Selected digest written to {} ({}/{} PDFs downloaded)",
                 digest_dir.string(), ok, picked.size());

    return digest_dir.string();
}

namespace {

// Quote a string for YAML frontmatter — wrap in double quotes and escape
// embedded backslashes / double quotes. Newlines are folded to spaces so a
// single-line value remains valid in flow style.
std::string yaml_quote(const std::string& s) {
    std::string out = "\"";
    out.reserve(s.size() + 2);
    for (char c : s) {
        if      (c == '\\') out += "\\\\";
        else if (c == '"')  out += "\\\"";
        else if (c == '\n' || c == '\r') out += ' ';
        else                out += c;
    }
    out += '"';
    return out;
}

// Split a comma-separated category string into a YAML inline list.
// "hep-ph, hep-lat" -> "[hep-ph, hep-lat]"
std::string yaml_inline_list(const std::string& csv) {
    std::string out = "[";
    bool first = true;
    std::string token;
    auto flush = [&]() {
        // trim
        size_t a = token.find_first_not_of(" \t");
        size_t b = token.find_last_not_of(" \t");
        if (a == std::string::npos) { token.clear(); return; }
        std::string t = token.substr(a, b - a + 1);
        token.clear();
        if (t.empty()) return;
        if (!first) out += ", ";
        out += t;
        first = false;
    };
    for (char c : csv) {
        if (c == ',') flush();
        else token += c;
    }
    flush();
    out += "]";
    return out;
}

} // namespace

std::string AppCore::ExportSelectedToObsidian() {
    const std::string& vault = m_config.get_obsidian_vault();
    if (vault.empty()) {
        spdlog::warn("[AppCore]: ExportSelectedToObsidian: no vault configured");
        return "";
    }
    if (m_selected_links.empty()) {
        spdlog::warn("[AppCore]: ExportSelectedToObsidian: no selections");
        return "";
    }

    // Resolve selections to full Article rows.
    auto all = m_db->GetRecent(-1);
    std::vector<Article> picked;
    picked.reserve(m_selected_links.size());
    for (const auto& a : all) {
        if (m_selected_links.count(a.link)) picked.push_back(a);
    }
    if (picked.empty()) {
        spdlog::warn("[AppCore]: ExportSelectedToObsidian: no matching articles in DB");
        return "";
    }

    namespace fs = std::filesystem;
    const std::string date_str = today_string();
    fs::path note_dir = fs::path(vault) / "arxiv-tui" / date_str;
    std::error_code ec;
    fs::create_directories(note_dir, ec);
    if (ec) {
        spdlog::error("[AppCore]: Cannot create vault dir {}: {}", note_dir.string(), ec.message());
        return "";
    }

    // Per-paper notes with YAML frontmatter + abstract + PDF embed.
    int pdf_ok = 0;
    for (const auto& a : picked) {
        const std::string aid = a.id();
        fs::path note_path = note_dir / (aid + ".md");

        std::ofstream nf(note_path);
        if (!nf.is_open()) {
            spdlog::error("[AppCore]: Cannot write note {}", note_path.string());
            continue;
        }
        nf << "---\n";
        nf << "title: "    << yaml_quote(a.title)   << "\n";
        nf << "authors: "  << yaml_quote(a.authors) << "\n";
        nf << "arxiv_id: " << aid                   << "\n";
        nf << "url: "      << yaml_quote(a.link)    << "\n";
        if (!a.category.empty()) {
            nf << "tags: "     << yaml_inline_list(a.category) << "\n";
            nf << "category: " << yaml_quote(a.category)       << "\n";
        }
        nf << "imported: "  << date_str << "\n";
        nf << "related-projects: []\n";
        nf << "---\n\n";
        nf << "# " << a.title << "\n\n";
        nf << "**Authors:** " << a.authors << "\n\n";
        nf << "**Link:** [" << a.link << "](" << a.link << ")\n\n";
        // Obsidian PDF embed: ![[file.pdf]] renders inline when colocated.
        nf << "![[" << aid << ".pdf]]\n\n";
        if (!a.abstract.empty()) {
            nf << "## Abstract\n\n" << a.abstract << "\n";
        }
        nf << "\n## Reading Notes\n\n"
           << "**Relevance:**\n\n"
           << "**Key ideas:**\n\n"
           << "**Open questions:**\n";

        // Download the PDF next to the note. Fetcher writes relative to
        // its own base_path (the configured download_dir), which is *not*
        // the vault. Use an absolute path via the std::filesystem API
        // instead so the file lands in the vault directory.
        fs::path pdf_path = note_dir / (aid + ".pdf");
        if (m_fetcher->DownloadPaper(aid, pdf_path.string())) ++pdf_ok;
    }

    // Index note that links to every paper imported today.
    fs::path index_path = note_dir / ("digest-" + date_str + ".md");
    std::ofstream idx(index_path);
    if (!idx.is_open()) {
        spdlog::error("[AppCore]: Cannot write index note {}", index_path.string());
        return "";
    }
    idx << "---\n";
    idx << "title: " << yaml_quote("arXiv digest " + date_str) << "\n";
    idx << "tags: [arxiv-digest]\n";
    idx << "date: " << date_str << "\n";
    idx << "---\n\n";
    idx << "# arXiv Digest — " << date_str << "\n\n";
    idx << picked.size() << " paper(s) imported.\n\n";
    for (const auto& a : picked) {
        // Wikilink with display text — Obsidian uses [[file|alias]].
        idx << "- [[" << a.id() << "|" << a.title << "]]\n";
    }
    idx.close();

    spdlog::info("[AppCore]: Obsidian digest written to {} ({}/{} PDFs)",
                 index_path.string(), pdf_ok, picked.size());
    return index_path.string();
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

void AppCore::StartAutoRefresh() {
    if (m_refresh_running.load()) return;
    m_refresh_running.store(true);
    m_refresh_thread = std::thread([this] {
        while (m_refresh_running.load()) {
            std::unique_lock<std::mutex> lock(m_refresh_mutex);
            int minutes = m_auto_refresh_minutes > 0 ? m_auto_refresh_minutes : 60;
            m_refresh_cv.wait_for(lock, std::chrono::minutes(minutes),
                                  [this] { return !m_refresh_running.load(); });
            if (m_refresh_running.load()) {
                FetchArticles();
            }
        }
    });
}

void AppCore::StopAutoRefresh() {
    if (!m_refresh_running.load()) return;
    m_refresh_running.store(false);
    m_refresh_cv.notify_all();
    if (m_refresh_thread.joinable()) {
        m_refresh_thread.join();
    }
}

bool AppCore::IsAutoRefreshing() const {
    return m_refresh_running.load();
}

int AppCore::GetAutoRefreshMinutes() const {
    return m_auto_refresh_minutes;
}

} // namespace Arxiv
