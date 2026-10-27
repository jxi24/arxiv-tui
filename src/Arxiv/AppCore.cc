// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"

#include "Arxiv/AtomicFile.hh"
#include "Arxiv/AuthorMatch.hh"
#include "Arxiv/BibTeX.hh"
#include "Arxiv/Export.hh"
#include "Arxiv/FuzzyMatch.hh"
#include "Arxiv/Process.hh"
#include "Arxiv/Replay.hh"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string_view>

#include "fmt/format.h"
#include "spdlog/spdlog.h"
#include "yaml-cpp/yaml.h"

namespace Arxiv {

// Parse a Unix timestamp held in the metadata table. Absent or malformed
// values yield 0, the "no anchor recorded" sentinel.
static std::int64_t parse_ts(const std::string& value) {
    if (value.empty())
        return 0;
    try {
        return std::stoll(value);
    } catch (const std::exception&) {
        spdlog::warn("[AppCore]: Ignoring malformed timestamp '{}'", value);
        return 0;
    }
}

static std::int64_t now_ts() { return std::time(nullptr); }

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
    , m_projects(*m_db,
                 [this](const Article& a) {
                     std::string bib = a.id().empty() ? "" : m_fetcher->FetchBibTeX(a.id());
                     return bib.empty() ? BibTeX::Build(a) : bib;
                 })
    , m_ranking(config.get_ranker_file())
    , m_retrain_interval(config.get_retrain_interval())
    , m_recommend_threshold(config.get_recommend_threshold())
    , m_auto_refresh_minutes(config.get_auto_refresh_minutes())
    , m_recorder(recorder) {
    m_undo_capacity = config.get_undo_buffer_size();
    m_undo_buffer.resize(m_undo_capacity);

    spdlog::info("ArxivAppCore Initialized");
    Record("appcore/ctor_begin");

    // Show every configured topic by default; user toggles via the filter
    // dialog.
    m_active_categories.insert(m_topics.begin(), m_topics.end());

    const std::string today = today_utc_string();
    const std::string prev_fetch = m_db->GetMetadata("last_fetch_date");
    std::string anchor = m_db->GetMetadata("new_articles_anchor");
    // "New" means new to the user: the window follows interactive sessions
    // only (last_view_*). A cron --fetch (Headless) fetches and stores but
    // must not move it, or the papers it stored would never count as new.
    // Databases from before last_view_* fall back to the fetch markers.
    std::string prev_view = m_db->GetMetadata("last_view_date");
    std::string prev_view_end = m_db->GetMetadata("last_view_end_ts");
    if (prev_view.empty()) {
        prev_view = prev_fetch;
        prev_view_end = m_db->GetMetadata("last_fetch_end_ts");
    }
    const bool crossed_day_boundary = (!prev_view.empty() && prev_view < today);

    // The "New Articles" anchor only advances on a real day-boundary
    // crossing. A same-day restart must NOT collapse the anchor to today —
    // doing so would push the window past every existing article and make the
    // New Articles view appear empty.
    //
    // The window is anchored on the instant the *previous* session finished
    // fetching, so it spans every day missed since then plus the current one.
    // Articles stored in between by a headless `--fetch` are covered too.
    // Advancing it to the end of this session's fetch instead would be a day
    // late; advancing it to "now" would replay articles the user already saw.
    //
    // Offline mode (headless exports, replay) computes the same window but
    // persists nothing, so it never consumes the interactive session's
    // crossing.
    const bool offline = (fetch_mode == FetchMode::Offline);
    const bool headless = (fetch_mode == FetchMode::Headless);
    m_new_articles_anchor_ts = parse_ts(m_db->GetMetadata("new_articles_anchor_ts"));
    if (crossed_day_boundary) {
        anchor = prev_view;
        m_new_articles_anchor_ts = parse_ts(prev_view_end);
        if (!offline && !headless) {
            m_db->SetMetadata("new_articles_anchor", anchor);
            m_db->SetMetadata("new_articles_anchor_ts", std::to_string(m_new_articles_anchor_ts));
        }
    }
    m_new_articles_since_date = anchor;

    Record("appcore/metadata_loaded", "prev_fetch=" + prev_fetch + " anchor=" + anchor);

    // Prune old articles before showing the list.
    if (!offline && m_config.get_max_article_age_days() > 0)
        m_db->PruneArticles(m_config.get_max_article_age_days());

    // Show whatever is already in the local DB immediately.
    RefreshFilterOptions();
    FetchArticles();
    Record("appcore/initial_fetcharticles_done",
           "count=" + std::to_string(m_current_articles.size()));

    // Restore the saved model; train one if there is none but ratings exist.
    m_ranking.LoadOrTrain(m_db->GetRecent(-1), m_db->GetRatedArticles());
    Record("appcore/ranker_loaded", std::string("trained=") + (m_ranking.IsTrained() ? "1" : "0"));

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
    auto do_fetch = [this, prev_fetch, today, fetch_mode, headless]() {
        Record("appcore/bg_fetch_begin",
               "mode=" + std::string(fetch_mode == FetchMode::Async ? "async" : "sync"));
        std::vector<Article> articles;
        try {
            if (!prev_fetch.empty() && prev_fetch < today) {
                // Day-boundary crossing: backfill the days we were away.
                // FetchSince also folds in today's announcement, so this
                // single call covers backfill + today — no separate Fetch().
                articles = m_fetcher->FetchSince(prev_fetch);
            } else {
                articles = m_fetcher->Fetch();
            }
        } catch (const std::exception& e) {
            // Leave last_fetch_date alone so the next launch retries the
            // same backfill window instead of skipping the missed days.
            spdlog::error("[AppCore]: Fetch failed: {}", e.what());
            Record("appcore/bg_fetch_failed", e.what());
            m_fetching.store(false);
            return;
        }
        Record("appcore/bg_fetch_returned", "n=" + std::to_string(articles.size()));

        // Wrap the bulk insert in a single transaction. Without this each
        // AddArticle is its own commit (ms-scale fsync each); the UI-thread
        // DB read could block for the entire window.
        Record("appcore/bg_db_insert_begin");
        m_db->AddArticles(articles);
        Record("appcore/bg_db_insert_end");

        // Stamped after the insert so the next session's New Articles window
        // starts past everything this fetch stored — otherwise today's papers
        // would be replayed as new tomorrow.
        const std::string end_ts = std::to_string(now_ts());
        m_db->SetMetadata("last_fetch_end_ts", end_ts);
        // Only now are the days up to today covered.
        m_db->SetMetadata("last_fetch_date", today);
        if (!headless) {
            // What the next session's New Articles window starts from.
            m_db->SetMetadata("last_view_end_ts", end_ts);
            m_db->SetMetadata("last_view_date", today);
        }
        m_last_fetch_ok.store(true);

        if (fetch_mode == FetchMode::Sync) {
            // Sync callers expect m_current_articles to reflect the fetch
            // immediately after the constructor returns.
            FetchArticles();
        } else {
            // UI thread will pick this up via TryRefetchIfNeeded.
            m_needs_refetch.store(true);
        }
        m_fetching.store(false);
        Record("appcore/bg_fetch_done");
    };

    if (offline) {
        Record("appcore/ctor_end", "offline");
        return;
    }
    m_fetching.store(true);
    if (fetch_mode == FetchMode::Async) {
        Record("appcore/fetch_thread_spawning");
        m_initial_fetch_thread = std::thread(std::move(do_fetch));
    } else {
        do_fetch();
    }
    Record("appcore/ctor_end");
}

AppCore::~AppCore() {
    Record("appcore/dtor_begin", std::string("fetching=") + (m_fetching.load() ? "1" : "0"));
    StopAutoRefresh();
    WaitForInitialFetch();
    WaitForBackgroundTask();
    // The training thread's completion callback uses this object.
    m_ranking.WaitForTraining();
    Record("appcore/dtor_end");
}

void AppCore::Record(const std::string& name, const std::string& detail) const {
    if (m_recorder)
        m_recorder->RecordEvent(name, detail);
}

void AppCore::WaitForInitialFetch() {
    if (m_initial_fetch_thread.joinable()) {
        m_initial_fetch_thread.join();
    }
}

void AppCore::ToggleCategory(const std::string& cat) {
    if (m_active_categories.count(cat))
        m_active_categories.erase(cat);
    else
        m_active_categories.insert(cat);
    FetchArticles();
}

void AppCore::SetActiveCategories(const std::set<std::string>& cats) {
    m_active_categories = cats;
    FetchArticles();
}

namespace {

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos)
        return "";
    auto e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

// Split "a, b, c" into trimmed, non-empty parts.
std::vector<std::string> split_list(const std::string& csv) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= csv.size()) {
        auto comma = csv.find(',', start);
        auto part =
            trim(csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!part.empty())
            parts.push_back(part);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return parts;
}

} // namespace

bool AppCore::MatchesActiveCategory(const std::string& categories) const {
    // Whole names only: "math" covers its subject classes ("math.AG") but not
    // the separate archive "math-ph".
    for (const auto& cat : split_list(categories)) {
        for (const auto& active : m_active_categories) {
            if (cat == active || cat.rfind(active + ".", 0) == 0)
                return true;
        }
    }
    return false;
}

void AppCore::FetchArticles() {
    Record("appcore/fetcharticles_begin",
           "view=" + std::to_string(static_cast<int>(GetFilterView())));
    // Reloading the same view keeps the cursor on the paper it was on (or,
    // if that paper is gone, on the same row); another view starts at the
    // top.
    const bool same_view = m_fetched_filter_index == m_filter_index;
    const int index_before = m_article_index;
    const std::string focused_before =
        same_view && index_before >= 0 && index_before < static_cast<int>(m_current_articles.size())
            ? m_current_articles[static_cast<std::size_t>(index_before)].link
            : std::string();
    m_fetched_filter_index = m_filter_index;
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
            m_current_articles =
                m_db->GetArticlesForDateRange(m_date_range.start, m_date_range.end);
        } else {
            m_current_articles = m_db->GetRecent(-1);
        }
        break;
    case FilterView::Search:
        if (m_search.active) {
            m_current_articles = m_db->SearchArticles(
                m_search.query, m_search.title, m_search.authors, m_search.abstract);
            // No exact hit (a typo, a different word form): near misses.
            if (m_current_articles.empty())
                m_current_articles = FuzzySearch(m_search);
        } else {
            m_current_articles = m_db->GetRecent(-1);
        }
        break;
    case FilterView::Recommended: {
        auto today_articles = m_db->GetRecent(1);
        // Drop replacements, exactly as New Articles does. A paper whose new
        // version is announced today and which was not already stored is
        // inserted straight from the RSS feed with today's announcement date,
        // so years-old papers land in GetRecent(1). Recommending a v3 of a 2022
        // paper as today's reading is never what the user asked for, and it
        // made the two views disagree about what counts as today's output.
        today_articles.erase(std::remove_if(today_articles.begin(),
                                            today_articles.end(),
                                            [](const Article& a) { return a.is_replacement; }),
                             today_articles.end());
        // Keywords rank before any ratings exist (blended score).
        if (m_ranking.CanRank())
            m_current_articles = m_ranking.Rank(std::move(today_articles), m_recommend_threshold);
        else
            m_current_articles = std::move(today_articles);
        break;
    }
    case FilterView::FollowedAuthors:
        m_current_articles = GetArticlesForFollowedAuthors();
        break;
    case FilterView::NewArticles: {
        // "New" is an insertion-time question, not a submission-date one. The
        // backfill query stamps each paper with its v1 submission date, which
        // for a paper announced during a missed day routinely predates the last
        // session — a submission-date window drops exactly the articles this
        // view exists to surface, leaving only the current day.
        if (m_new_articles_anchor_ts > 0)
            m_current_articles = m_db->GetArticlesAddedSince(m_new_articles_anchor_ts);

        if (m_current_articles.empty()) {
            // Either no insertion-time anchor is on record yet (fresh database,
            // or first run after the upgrade), or nothing has been stored since
            // the last session: the fetch may still be running, or arXiv may not
            // have announced yet. Fall back to the submission-date window so the
            // view is never blank.
            if (m_new_articles_since_date.empty())
                m_current_articles = m_db->GetRecent(-1);
            else
                m_current_articles = m_db->GetArticlesSince(m_new_articles_since_date);
        }
        // Drop replacements (later versions of older submissions) — the
        // New Articles view is meant for truly fresh papers only.
        m_current_articles.erase(std::remove_if(m_current_articles.begin(),
                                                m_current_articles.end(),
                                                [](const Article& a) { return a.is_replacement; }),
                                 m_current_articles.end());
        // With ratings or keywords, the most relevant new articles come
        // first. No threshold: the user wants *every* new paper, in order.
        if (m_ranking.CanRank() && m_current_articles.size() > 1)
            m_current_articles = m_ranking.Rank(std::move(m_current_articles));
        break;
    }
    case FilterView::Updated:
        // The other half of the last fetch: papers whose new version the feed
        // announced since the previous session. Same anchor as New Articles, so
        // nothing the fetch carried falls between the two views. No fallback to
        // a date window — an empty Updated view is a true statement (nothing was
        // re-announced), unlike an empty New Articles view on a fresh database.
        if (m_new_articles_anchor_ts > 0)
            m_current_articles = m_db->GetArticlesUpdatedSince(m_new_articles_anchor_ts);
        break;
    case FilterView::Unread:
        m_current_articles = m_db->GetUnreadArticles();
        break;
    case FilterView::Tag:
        m_current_articles = m_db->GetArticlesForTag(GetTagNameForFilter(m_filter_index));
        break;
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
        m_current_articles.erase(std::remove_if(m_current_articles.begin(),
                                                m_current_articles.end(),
                                                [this](const Article& a) {
                                                    return !a.category.empty() &&
                                                           !MatchesActiveCategory(a.category);
                                                }),
                                 m_current_articles.end());
    }

    RefreshTitles();
    m_article_index = 0;
    if (same_view && !m_current_articles.empty()) {
        auto it = std::find_if(m_current_articles.begin(),
                               m_current_articles.end(),
                               [&](const Article& a) { return a.link == focused_before; });
        m_article_index =
            it != m_current_articles.end()
                ? static_cast<int>(it - m_current_articles.begin())
                : std::min(index_before, static_cast<int>(m_current_articles.size()) - 1);
    }
    Record("appcore/fetcharticles_end", "count=" + std::to_string(m_current_articles.size()));
    NotifyArticleUpdate();
}

void AppCore::ToggleBookmark(const std::string& article_link) {
    auto it = std::find_if(m_current_articles.begin(),
                           m_current_articles.end(),
                           [&](const Article& a) { return a.link == article_link; });

    if (it != m_current_articles.end()) {
        it->bookmarked = !it->bookmarked;
        m_db->ToggleBookmark(article_link, it->bookmarked);
        RefreshTitles();
        NotifyArticleUpdate();
    }
}

bool AppCore::DownloadArticle(const std::string& article_id) {
    // Absolute path under the current download_dir (which Settings can
    // change at runtime); '/' in old-style ids would otherwise make a
    // subdirectory.
    std::string stem = article_id;
    std::replace(stem.begin(), stem.end(), '/', '_');
    auto path = std::filesystem::path(m_config.get_download_dir()) / (stem + ".pdf");
    return m_fetcher->DownloadPaper(article_id, path.string());
}

std::optional<Article> AppCore::FindArticle(const std::string& link) const {
    auto it = std::find_if(m_current_articles.begin(),
                           m_current_articles.end(),
                           [&](const Article& a) { return a.link == link; });
    if (it != m_current_articles.end())
        return *it;
    return m_db->GetArticle(link);
}

std::vector<Article> AppCore::SelectedArticles() const {
    std::vector<Article> picked;
    picked.reserve(m_selected_links.size());
    for (const auto& link : m_selected_links)
        if (auto a = FindArticle(link))
            picked.push_back(std::move(*a));
    return picked;
}

std::string AppCore::GetBibtex(const Article& article) {
    if (article.id().empty())
        return "";
    std::string bibtex = m_fetcher->FetchBibTeX(article.id());
    return bibtex.empty() ? BibTeX::Build(article) : bibtex;
}

const std::vector<Article>& AppCore::GetCurrentArticles() const { return m_current_articles; }

const std::vector<std::string>& AppCore::GetCurrentTitles() const { return m_current_titles; }

void AppCore::AddProject(const std::string& project_name) {
    if (!m_projects.Add(project_name))
        return; // nothing left after trimming
    RefreshFilterOptions();
    if (m_project_update_callback)
        m_project_update_callback();
    NotifyArticleUpdate();
}

void AppCore::RemoveProject(const std::string& project_name) {
    m_projects.Remove(project_name);
    const int index_before = m_filter_index;
    RefreshFilterOptions();
    if (m_project_update_callback)
        m_project_update_callback();
    // The viewed project is gone (or moved): show what the filter now says.
    if (m_filter_index != index_before)
        FetchArticles();
    else
        NotifyArticleUpdate();
}

void AppCore::LinkArticleToProject(const std::string& article_link,
                                   const std::string& project_name) {
    m_projects.Link(project_name, {article_link});
    NotifyArticleUpdate();
}

void AppCore::UnlinkArticleFromProject(const std::string& article_link,
                                       const std::string& project_name) {
    m_projects.Unlink(project_name, article_link);
    NotifyArticleUpdate();
}

std::vector<std::string> AppCore::GetProjects() const { return m_projects.All(); }

// ---------------------------------------------------------------------------
// Tag management
// ---------------------------------------------------------------------------

void AppCore::AddTag(const std::string& name) {
    m_db->AddTag(name);
    RefreshFilterOptions();
    FetchArticles();
}

void AppCore::RemoveTag(const std::string& name) {
    m_db->RemoveTag(name);
    RefreshFilterOptions();
    FetchArticles();
}

std::vector<std::string> AppCore::GetTags() const { return m_db->GetTags(); }

std::vector<std::string> AppCore::GetTagsForArticle(const std::string& link) const {
    return m_db->GetTagsForArticle(link);
}

void AppCore::LinkArticleToTag(const std::string& link, const std::string& tag) {
    const auto tags_before = m_db->GetTags();
    m_db->LinkArticleToTag(link, tag); // creates the tag if new
    if (std::find(tags_before.begin(), tags_before.end(), tag) == tags_before.end())
        RefreshFilterOptions();
    NotifyArticleUpdate();
}

void AppCore::UnlinkArticleFromTag(const std::string& link, const std::string& tag) {
    m_db->UnlinkArticleFromTag(link, tag);
    NotifyArticleUpdate();
}

AppCore::Coverage AppCore::TagCoverage(const std::string& tag) const {
    std::size_t tagged = 0;
    const auto targets = TargetLinks();
    for (const auto& link : targets) {
        const auto tags = m_db->GetTagsForArticle(link);
        if (std::find(tags.begin(), tags.end(), tag) != tags.end())
            ++tagged;
    }
    if (tagged == 0)
        return Coverage::None;
    return tagged == targets.size() ? Coverage::All : Coverage::Some;
}

void AppCore::ToggleTagOnTargets(const std::string& tag) {
    const bool remove = TagCoverage(tag) == Coverage::All;
    const auto tags_before = m_db->GetTags();
    for (const auto& link : TargetLinks()) {
        if (remove)
            m_db->UnlinkArticleFromTag(link, tag);
        else
            m_db->LinkArticleToTag(link, tag);
    }
    if (std::find(tags_before.begin(), tags_before.end(), tag) == tags_before.end())
        RefreshFilterOptions(); // a new tag gets its filter entry now
    // The tag's own view changes when its articles do.
    if (CurrentTag() == tag)
        FetchArticles();
    else
        NotifyArticleUpdate();
}

std::string AppCore::GetTagNameForFilter(int index) const {
    int tag_idx = index - kFirstTagIndex;
    if (tag_idx >= 0 && static_cast<size_t>(tag_idx) < m_filter_tag_names.size())
        return m_filter_tag_names[static_cast<size_t>(tag_idx)];
    return "";
}

std::vector<Article> AppCore::GetArticlesForProject(const std::string& project_name) const {
    return m_projects.Articles(project_name);
}

const std::vector<std::string>& AppCore::GetFilterOptions() const { return m_filter_options; }

void AppCore::SetFilterIndex(int index) {
    // Clamp to the valid range so callers cannot reach an unhandled branch.
    if (!m_filter_options.empty()) {
        int max_idx = static_cast<int>(m_filter_options.size()) - 1;
        index = std::max(0, std::min(index, max_idx));
    } else {
        index = 0;
    }
    if (index != m_filter_index) {
        m_filter_index = index;
        FetchArticles();
    }
}

int AppCore::GetFilterIndex() const { return m_filter_index; }

std::string AppCore::CurrentTag() const {
    return GetFilterView() == FilterView::Tag ? GetTagNameForFilter(m_filter_index) : "";
}

std::string AppCore::CurrentProject() const {
    return GetFilterView() == FilterView::Project ? GetProjectNameForFilter(m_filter_index) : "";
}

AppCore::FilterView AppCore::GetFilterView() const {
    if (m_filter_index >= m_project_start_index)
        return FilterView::Project;
    if (m_filter_index >= kFirstTagIndex && !m_filter_tag_names.empty())
        return FilterView::Tag;
    return static_cast<FilterView>(m_filter_index);
}

void AppCore::MarkArticleRead(const std::string& link) {
    m_db->MarkArticleRead(link);
    auto it = std::find_if(m_current_articles.begin(),
                           m_current_articles.end(),
                           [&](const Article& a) { return a.link == link; });
    if (it != m_current_articles.end())
        it->read = true;
    m_filter_counts_stale = true;
    RefreshTitles();
}

void AppCore::SetFilterIndex(FilterView view) { SetFilterIndex(static_cast<int>(view)); }

void AppCore::SetArticleIndex(int index) {
    // Every caller indexes m_current_articles with this, so keep it in range.
    const int last = static_cast<int>(m_current_articles.size()) - 1;
    index = std::max(0, std::min(index, last));
    if (index != m_article_index) {
        m_article_index = index;
        NotifyArticleUpdate();
    }
}

int AppCore::GetArticleIndex() const { return m_article_index; }

bool AppCore::IsArticleBookmarked(const std::string& article_link) const {
    auto it = std::find_if(m_current_articles.begin(),
                           m_current_articles.end(),
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
    // Plain titles: selection and bookmark markers are drawn by the view.
    for (const auto& article : m_current_articles)
        m_current_titles.push_back(article.title);
}

std::optional<int> AppCore::GetFilterCount(int index) const {
    if (m_filter_counts_stale && !m_fetching.load()) {
        m_filter_counts = m_db->CountFilters();
        m_filter_counts_stale = false;
    }
    auto lookup = [](const std::map<std::string, int>& m, const std::string& name) {
        auto it = m.find(name);
        return it == m.end() ? 0 : it->second;
    };
    if (index == static_cast<int>(FilterView::Bookmarks))
        return m_filter_counts.bookmarked;
    if (index == static_cast<int>(FilterView::Unread))
        return m_filter_counts.unread;
    if (index >= m_project_start_index &&
        index < m_project_start_index + static_cast<int>(m_filter_project_names.size()))
        return lookup(m_filter_counts.projects, GetProjectNameForFilter(index));
    if (index >= kFirstTagIndex && index < m_project_start_index)
        return lookup(m_filter_counts.tags, GetTagNameForFilter(index));
    return std::nullopt;
}

std::string AppCore::GetFilterSection(int index) const {
    if (index == kFirstTagIndex && !m_filter_tag_names.empty())
        return "Tags";
    if (index == m_project_start_index && !m_filter_project_names.empty())
        return "Projects";
    return "";
}

void AppCore::RefreshFilterOptions() {
    m_filter_counts_stale = true;
    // Tags and projects are addressed by position, and adding or removing one
    // shifts everything after it. Remember what is selected by name and
    // re-select it once the list is rebuilt.
    const FilterView view_before = GetFilterView();
    const std::string tag_before =
        view_before == FilterView::Tag ? GetTagNameForFilter(m_filter_index) : "";
    const std::string project_before =
        view_before == FilterView::Project ? GetProjectNameForFilter(m_filter_index) : "";

    m_filter_options = {"All Articles",
                        "Bookmarks",
                        "Today",
                        "Range",
                        "Search",
                        "Recommended",
                        "Followed Authors",
                        "New Articles",
                        "Updated",
                        "Unread"};
    m_filter_tag_names.clear();
    m_filter_project_names.clear();

    // Add tags (indices kFirstTagIndex, +1, ...)
    for (const auto& tag : m_db->GetTags()) {
        m_filter_options.push_back("#" + tag);
        m_filter_tag_names.push_back(tag);
    }
    m_project_start_index = kFirstTagIndex + static_cast<int>(m_filter_tag_names.size());

    // Projects as a tree, depth-first, children indented two spaces per
    // level. A project whose parent no longer exists is shown at the top
    // level, and a parent cycle cannot hide or repeat a project.
    auto all_projects = m_db->GetProjects();
    std::set<std::string> known(all_projects.begin(), all_projects.end());
    std::vector<std::string> roots;
    std::map<std::string, std::vector<std::string>> children;
    for (const auto& name : all_projects) {
        std::string parent = m_db->GetProjectParent(name);
        if (parent.empty() || parent == name || !known.count(parent))
            roots.push_back(name);
        else
            children[parent].push_back(name);
    }

    std::set<std::string> shown;
    std::function<void(const std::string&, int)> add = [&](const std::string& name, int depth) {
        if (!shown.insert(name).second)
            return;
        m_filter_options.push_back(std::string(static_cast<std::size_t>(depth) * 2, ' ') + name);
        m_filter_project_names.push_back(name);
        auto it = children.find(name);
        if (it != children.end())
            for (const auto& child : it->second)
                add(child, depth + 1);
    };
    for (const auto& root : roots)
        add(root, 0);
    // Members of a parent cycle have no root; list them rather than lose them.
    for (const auto& name : all_projects)
        add(name, 0);

    auto reselect = [&](const std::vector<std::string>& names, const std::string& name, int base) {
        auto it = std::find(names.begin(), names.end(), name);
        // Gone (deleted): fall back to New Articles rather than a neighbour.
        m_filter_index = it == names.end() ? static_cast<int>(FilterView::NewArticles)
                                           : base + static_cast<int>(it - names.begin());
    };
    if (!tag_before.empty())
        reselect(m_filter_tag_names, tag_before, kFirstTagIndex);
    else if (!project_before.empty())
        reselect(m_filter_project_names, project_before, m_project_start_index);
}

void AppCore::SetDateRange(const std::string& start, const std::string& end) {
    m_date_range.set(start, end);
    m_fetched_filter_index = -1; // new results: start at the top
    FetchArticles();
}

void AppCore::ClearDateRange() {
    m_date_range.clear();
    m_fetched_filter_index = -1; // new results: start at the top
    FetchArticles();
}

void AppCore::SetSearchQuery(const std::string& query,
                             bool _search_title,
                             bool _search_authors,
                             bool _search_abstract) {
    // No field ticked means titles, as before.
    const bool any = _search_title || _search_authors || _search_abstract;
    m_search.set(query, _search_title || !any, _search_authors, _search_abstract);
    m_fetched_filter_index = -1; // new results: start at the top
    FetchArticles();
}

void AppCore::ClearSearch() {
    m_search.clear();
    m_fetched_filter_index = -1; // new results: start at the top
    FetchArticles();
}

void AppCore::RateArticle(const std::string& article_link, int rating) {
    if (rating < 1 || rating > 5)
        return;
    m_db->SetRating(article_link, rating);
    spdlog::info("[AppCore]: Rated article {} with {}", article_link, rating);

    ++m_ratings_since_train;
    spdlog::debug("[AppCore]: {} new rating(s) pending (threshold: {})",
                  m_ratings_since_train,
                  m_retrain_interval);

    if (m_ratings_since_train >= m_retrain_interval) {
        m_ratings_since_train = 0;
        SpawnTrainingThread(/*warm_start=*/true);
    } else {
        // Not at threshold yet — just refresh the UI so the rating display
        // updates without triggering training.
        NotifyArticleUpdate();
    }
}

void AppCore::RateSelected(int rating) {
    if (rating < 1 || rating > 5)
        return;

    const std::vector<std::string> targets = TargetLinks();

    for (const auto& link : targets) {
        m_db->SetRating(link, rating);
        spdlog::info("[AppCore]: Rated article {} with {}", link, rating);
        ++m_ratings_since_train;
    }

    spdlog::debug("[AppCore]: {} new rating(s) pending (threshold: {})",
                  m_ratings_since_train,
                  m_retrain_interval);

    if (m_ratings_since_train >= m_retrain_interval) {
        m_ratings_since_train = 0;
        SpawnTrainingThread(/*warm_start=*/true);
    } else {
        NotifyArticleUpdate();
    }
}

std::string AppCore::ExportPath(const std::string& file_name, bool create) const {
    namespace fs = std::filesystem;
    fs::path dir = m_config.get_export_dir().empty() ? fs::path(m_config.get_download_dir())
                                                     : fs::path(m_config.get_export_dir());
    // One safe path component: project names may contain '/' (or be "..").
    std::string name;
    for (char c : file_name) {
        const auto u = static_cast<unsigned char>(c);
        name += (c == '/' || c == '\\' || u < 0x20 || u == 0x7f) ? '_' : c;
    }
    name.erase(0, name.find_first_not_of('.'));
    if (name.empty())
        name = "export";
    std::error_code ec;
    if (create)
        fs::create_directories(dir, ec);
    if (ec)
        spdlog::error(
            "[AppCore]: Cannot create export directory {}: {}", dir.string(), ec.message());
    return (dir / name).string();
}

void AppCore::ForceRetrain() {
    spdlog::info("[AppCore]: Full retrain requested");
    m_ratings_since_train = 0;
    SpawnTrainingThread(/*warm_start=*/false);
}

void AppCore::SpawnTrainingThread(bool warm_start) {
    // Snapshot the training data here, on the calling (UI) thread. The UI
    // thread re-queries on its next tick (TryRefetchIfNeeded); the update
    // callback is never invoked from the training thread.
    m_ranking.StartTraining(m_db->GetRecent(-1), m_db->GetRatedArticles(), warm_start, [this] {
        m_needs_refetch = true;
    });
}

bool AppCore::IsRankerTrained() const { return m_ranking.IsTrained(); }

bool AppCore::TryRefetchIfNeeded() {
    // Articles a background download marked read: update the list in place
    // rather than re-querying (which would reset the cursor).
    std::vector<std::string> newly_read;
    {
        std::lock_guard<std::mutex> lock(m_task_mutex);
        newly_read.swap(m_pending_read);
    }
    bool changed = false;
    if (!newly_read.empty()) {
        for (auto& a : m_current_articles)
            if (std::find(newly_read.begin(), newly_read.end(), a.link) != newly_read.end())
                a.read = true;
        m_filter_counts_stale = true;
        RefreshTitles();
        changed = true;
    }

    // Don't query the DB while the background fetch is still writing —
    // SQLite would serialise the read against every pending insert and the
    // UI thread would block for the duration of the bulk insert.
    if (m_fetching.load()) {
        Record("appcore/try_refetch_skipped", "fetching");
        return changed;
    }
    if (m_needs_refetch.exchange(false)) {
        Record("appcore/try_refetch_executing");
        FetchArticles(); // FetchArticles also fires NotifyArticleUpdate
        Record("appcore/try_refetch_executed",
               "count=" + std::to_string(m_current_articles.size()));
        changed = true;
    }
    return changed;
}

int AppCore::GetArticleRating(const std::string& article_link) const {
    return m_db->GetRating(article_link);
}

float AppCore::GetPredictedScore(const Article& article) const { return m_ranking.Score(article); }

void AppCore::SetRecommendThreshold(float threshold) {
    m_recommend_threshold = threshold;
    if (GetFilterView() == FilterView::Recommended) {
        FetchArticles();
    }
}

void AppCore::NotifyArticleUpdate() {
    m_filter_counts_stale = true;
    if (m_article_update_callback) {
        m_article_update_callback();
    }
}

std::vector<std::string> AppCore::GetProjectsForArticle(const std::string& article_link) const {
    return m_projects.ProjectsFor(article_link);
}

void AppCore::SetProjectParent(const std::string& project_name, const std::string& parent) {
    m_projects.SetParent(project_name, parent);
    RefreshFilterOptions();
    if (m_project_update_callback)
        m_project_update_callback();
    NotifyArticleUpdate();
}

void AppCore::SetProjectNote(const std::string& project_name,
                             const std::string& article_link,
                             const std::string& note) {
    m_projects.SetNote(project_name, article_link, note);
    NotifyArticleUpdate();
}

std::string AppCore::GetProjectNote(const std::string& project_name,
                                    const std::string& article_link) const {
    return m_projects.Note(project_name, article_link);
}

std::string AppCore::GetProjectNameForFilter(int index) const {
    int proj_index = index - m_project_start_index; // 0-based into project list
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

// ---------------------------------------------------------------------------
// Exports — data gathering here, file formats in Export.cc
// ---------------------------------------------------------------------------

bool AppCore::ExportProjectMarkdown(const std::string& project_name,
                                    const std::string& output_path) const {
    return m_projects.ExportMarkdown(project_name, output_path);
}

bool AppCore::ExportProjectText(const std::string& project_name,
                                const std::string& output_path) const {
    return m_projects.ExportText(project_name, output_path);
}

bool AppCore::ExportProjectJSON(const std::string& project_name,
                                const std::string& output_path) const {
    return m_projects.ExportJSON(project_name, output_path);
}

bool AppCore::ImportProjectJSON(const std::string& input_path) {
    if (!m_projects.ImportJSON(input_path))
        return false;
    RefreshFilterOptions();
    if (m_project_update_callback)
        m_project_update_callback();
    NotifyArticleUpdate();
    return true;
}

// BibTeX: InspireHEP when it has the paper, else BibTeX::Build.
bool AppCore::ExportArticlesBibTeX(const std::vector<Article>& articles,
                                   const std::string& output_path) const {
    return m_projects.ExportArticlesBibTeX(articles, output_path);
}

bool AppCore::ExportArticleBibTeX(const Article& article, const std::string& output_path) const {
    return ExportArticlesBibTeX({article}, output_path);
}

bool AppCore::ExportProjectBibTeX(const std::string& project_name, const std::string& output_path) {
    return m_projects.ExportBibTeX(project_name, output_path);
}

// ---------------------------------------------------------------------------
// Fuzzy search
// ---------------------------------------------------------------------------

std::vector<Article> AppCore::FuzzySearchArticles(const std::string& query, int threshold) const {
    SearchFilter all_fields;
    all_fields.set(query, true, true, true);
    return FuzzySearch(all_fields, threshold);
}

std::vector<Article> AppCore::FuzzySearch(const SearchFilter& search, int threshold) const {
    std::vector<Article> results;
    if (search.query.empty())
        return results;
    for (auto& a : m_db->GetRecent(-1)) {
        if ((search.title && FuzzyMatch::MatchesText(search.query, a.title, threshold)) ||
            (search.authors && FuzzyMatch::MatchesText(search.query, a.authors, threshold)) ||
            (search.abstract && FuzzyMatch::MatchesText(search.query, a.abstract, threshold)))
            results.push_back(std::move(a));
    }
    return results;
}

// ---------------------------------------------------------------------------
// Author subscriptions
// ---------------------------------------------------------------------------

void AppCore::FollowAuthor(const std::string& author_name) {
    m_db->FollowAuthor(author_name);
    if (GetFilterView() == FilterView::FollowedAuthors)
        FetchArticles(); // the view is defined by the followed list
}

void AppCore::UnfollowAuthor(const std::string& author_name) {
    m_db->UnfollowAuthor(author_name);
    if (GetFilterView() == FilterView::FollowedAuthors)
        FetchArticles();
}

std::vector<std::string> AppCore::GetFollowedAuthors() const { return m_db->GetFollowedAuthors(); }

std::vector<Article> AppCore::GetArticlesForFollowedAuthors() const {
    auto followed = m_db->GetFollowedAuthors();
    if (followed.empty())
        return {};

    auto all = m_db->GetRecent(-1);
    std::vector<Article> result;
    // Whole names with initials (see AuthorMatches): "W. Li" picks out Wei Li
    // but not Xiao Li or Wei Lin.
    for (const auto& a : all) {
        const auto names = split_list(a.authors);
        bool match = std::any_of(names.begin(), names.end(), [&](const std::string& name) {
            return std::any_of(followed.begin(), followed.end(), [&](const std::string& f) {
                return AuthorMatches(f, name);
            });
        });
        if (match)
            result.push_back(a);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Daily-digest export
// ---------------------------------------------------------------------------

bool AppCore::ExportDailyDigest(const std::string& output_path) const {
    return Export::DailyDigestMarkdown(output_path, m_db->GetRecent(1)); // last 24 hours
}

bool AppCore::ExportDailyDigestYAML(const std::string& output_path) const {
    // Highest predicted score first (trained model and/or interest keywords), so
    // consumers such as a pre-commute drive digest can take the top N as-is.
    // Only papers first posted today: a replacement is a new version of an older paper.
    auto recent = m_db->GetRecent(1);
    recent.erase(std::remove_if(recent.begin(),
                                recent.end(),
                                [](const Article& a) { return a.is_replacement; }),
                 recent.end());
    auto scored = m_ranking.ScoreAll(std::move(recent));
    std::stable_sort(scored.begin(), scored.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });
    return Export::DailyDigestYAML(output_path, scored);
}

// ---------------------------------------------------------------------------
// Article selection + curated digest
// ---------------------------------------------------------------------------

void AppCore::ToggleSelection(const std::string& link) {
    if (m_selected_links.count(link))
        m_selected_links.erase(link);
    else
        m_selected_links.insert(link);
    RefreshTitles();
    NotifyArticleUpdate();
}

void AppCore::ClearSelections() {
    m_selected_links.clear();
    RefreshTitles();
    NotifyArticleUpdate();
}

std::vector<std::string> AppCore::GetLinksToOpen() const {
    // These go to xdg-open: only real arXiv pages, whatever an older
    // database row holds (file://, javascript:, a leading "-").
    auto links = TargetLinks();
    links.erase(std::remove_if(links.begin(),
                               links.end(),
                               [](const std::string& l) { return !Fetcher::IsArxivLink(l); }),
                links.end());
    return links;
}

std::vector<std::string> AppCore::TargetLinks() const {
    if (!m_selected_links.empty())
        return {m_selected_links.begin(), m_selected_links.end()};
    if (m_article_index >= 0 &&
        static_cast<std::size_t>(m_article_index) < m_current_articles.size())
        return {m_current_articles[static_cast<std::size_t>(m_article_index)].link};
    return {};
}

void AppCore::DeleteCurrentOrSelected() {
    std::vector<std::string> to_delete = TargetLinks();

    if (to_delete.empty()) {
        return;
    }

    // Snapshot each article's full state before deletion so it can be restored.
    if (m_undo_capacity > 0) {
        UndoEntry entry;
        entry.reserve(to_delete.size());
        for (const auto& link : to_delete) {
            // Selections survive view changes, so a selected article may not
            // be in the current list; the DB has it either way.
            auto article = FindArticle(link);
            if (!article)
                continue;

            DeletedArticleSnapshot snap;
            snap.article = *article;
            snap.rating = m_db->GetRating(link);

            for (const auto& proj : m_db->GetProjectsForArticle(link)) {
                snap.project_notes.emplace_back(proj, m_db->GetProjectNote(proj, link));
            }
            snap.tags = m_db->GetTagsForArticle(link);
            entry.push_back(std::move(snap));
        }
        if (!entry.empty())
            PushUndo(std::move(entry));
    }

    for (const auto& link : to_delete) {
        spdlog::info("[AppCore]: Deleting article {}", link);
        m_db->DeleteArticle(link);
    }
    m_selected_links.clear();
    FetchArticles();
}

void AppCore::BookmarkSelected(bool bookmarked) {
    const std::vector<std::string> targets = TargetLinks();
    for (const auto& link : targets) {
        m_db->ToggleBookmark(link, bookmarked);
        auto it = std::find_if(m_current_articles.begin(),
                               m_current_articles.end(),
                               [&](const Article& a) { return a.link == link; });
        if (it != m_current_articles.end())
            it->bookmarked = bookmarked;
    }
    RefreshTitles();
    NotifyArticleUpdate();
}

void AppCore::AddSelectedToProject(const std::string& project_name) {
    m_projects.Link(project_name, TargetLinks()); // keeps the project's .bib current too
    NotifyArticleUpdate();
}

std::string AppCore::ExportSelectedDigest() {
    auto picked = SelectedArticles();
    if (picked.empty()) {
        spdlog::warn("[AppCore]: ExportSelectedDigest: nothing selected");
        return "";
    }
    return Export::SelectedDigest(*m_fetcher, picked, m_config.get_download_dir(), {});
}

std::string AppCore::ExportSelectedDigestArchive() {
    std::string dir = ExportSelectedDigest();
    return dir.empty() ? "" : Export::PackArchive(dir);
}

std::string AppCore::ExportSelectedToObsidian() {
    const std::string& vault = m_config.get_obsidian_vault();
    if (vault.empty()) {
        spdlog::warn("[AppCore]: ExportSelectedToObsidian: no vault configured");
        return "";
    }
    auto picked = SelectedArticles();
    if (picked.empty()) {
        spdlog::warn("[AppCore]: ExportSelectedToObsidian: nothing selected");
        return "";
    }
    return Export::ObsidianNotes(
        *m_fetcher, picked, vault, m_config.get_obsidian_download_pdfs(), {});
}

// ---------------------------------------------------------------------------
// Background tasks
//
// Everything a worker needs (articles, paths, flags) is captured on the UI
// thread when the task starts; the worker touches only the fetcher, the
// filesystem and the DB (which serialises access), and reports through
// m_task_status under m_task_mutex.
// ---------------------------------------------------------------------------

bool AppCore::StartTask(std::string label, std::size_t total, TaskWork work) {
    std::lock_guard<std::mutex> lock(m_task_mutex);
    if (m_task_status.running)
        return false;
    // A finished worker may still be returning; it no longer needs the lock.
    if (m_task_thread.joinable())
        m_task_thread.join();
    m_task_status = BackgroundTaskStatus{};
    m_task_status.running = true;
    m_task_status.label = std::move(label);
    m_task_status.total = total;
    m_task_thread = std::thread([this, work = std::move(work)] {
        auto progress = [this] {
            std::lock_guard<std::mutex> l(m_task_mutex);
            ++m_task_status.done;
        };
        TaskOutcome outcome;
        try {
            outcome = work(progress);
        } catch (const std::exception& e) {
            outcome.error = e.what();
        }
        std::lock_guard<std::mutex> l(m_task_mutex);
        m_task_status.result = std::move(outcome.result);
        m_task_status.error = std::move(outcome.error);
        m_task_status.message = std::move(outcome.message);
        m_task_status.finished_at = std::chrono::steady_clock::now();
        m_task_status.running = false;
    });
    return true;
}

bool AppCore::StartExportSelectedToObsidian() {
    const std::string vault = m_config.get_obsidian_vault();
    auto picked = SelectedArticles();
    if (vault.empty() || picked.empty())
        return false;
    const bool download = m_config.get_obsidian_download_pdfs();
    const std::size_t n = picked.size();
    return StartTask(
        "Exporting to Obsidian",
        n,
        [this, picked = std::move(picked), vault, download](const std::function<void()>& progress) {
            TaskOutcome out;
            out.result = Export::ObsidianNotes(*m_fetcher, picked, vault, download, progress);
            if (out.result.empty())
                out.error = "Obsidian export failed — check the vault path";
            else
                out.message = "Exported to Obsidian vault: " + out.result;
            return out;
        });
}

bool AppCore::StartExportSelectedDigest() { return StartDigestTask(false); }

bool AppCore::StartExportSelectedDigestArchive() { return StartDigestTask(true); }

bool AppCore::StartDigestTask(bool archive) {
    auto picked = SelectedArticles();
    if (picked.empty())
        return false;
    const std::string dir = m_config.get_download_dir();
    const std::size_t n = picked.size();
    return StartTask(
        archive ? "Building digest archive" : "Exporting selected digest",
        n,
        [this, picked = std::move(picked), dir, archive](const std::function<void()>& progress) {
            TaskOutcome out;
            out.result = Export::SelectedDigest(*m_fetcher, picked, dir, progress);
            if (out.result.empty()) {
                out.error = "Failed to export digest";
            } else if (archive) {
                out.result = Export::PackArchive(out.result);
                if (out.result.empty())
                    out.error = "Failed to create digest archive";
                else
                    out.message = "Archive exported to " + out.result;
            } else {
                out.message = "Digest exported to " + out.result;
            }
            return out;
        });
}

bool AppCore::StartDownloadArticle(const std::string& article_link) {
    auto article = FindArticle(article_link);
    if (!article)
        return false;
    const std::string id = article->id();
    const auto path =
        std::filesystem::path(m_config.get_download_dir()) / (article->file_stem() + ".pdf");
    return StartTask("Downloading " + id,
                     1,
                     [this, id, path, link = article_link](const std::function<void()>& progress) {
                         TaskOutcome out;
                         if (m_fetcher->DownloadPaper(id, path.string())) {
                             m_db->MarkArticleRead(link);
                             {
                                 std::lock_guard<std::mutex> l(m_task_mutex);
                                 m_pending_read.push_back(link);
                             }
                             out.result = path.string();
                             out.message = "Downloaded " + path.string();
                         } else {
                             out.error = "Failed to download article: " + id;
                         }
                         progress();
                         return out;
                     });
}

AppCore::BackgroundTaskStatus AppCore::GetBackgroundTaskStatus() const {
    std::lock_guard<std::mutex> lock(m_task_mutex);
    return m_task_status;
}

void AppCore::WaitForBackgroundTask() {
    if (m_task_thread.joinable())
        m_task_thread.join();
}

// ---------------------------------------------------------------------------
// Keyword management
// ---------------------------------------------------------------------------

void AppCore::ApplyConfig(const Config& config) {
    // Topics are fixed for the session: the fetcher (shared with background
    // threads) was built with them. Everything else applies immediately.
    const int old_refresh = m_auto_refresh_minutes;
    m_config = config;
    m_retrain_interval = config.get_retrain_interval();
    m_auto_refresh_minutes = config.get_auto_refresh_minutes();
    if (config.get_undo_buffer_size() != m_undo_capacity)
        SetUndoCapacity(config.get_undo_buffer_size()); // resizing drops the history
    SetRecommendThreshold(config.get_recommend_threshold());
    if (m_auto_refresh_minutes != old_refresh) {
        StopAutoRefresh();
        if (m_auto_refresh_minutes > 0)
            StartAutoRefresh();
    }
}

void AppCore::ReloadKeywords() {
    const std::string& path = m_config.get_keywords_file();
    if (path.empty())
        return;

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
    spdlog::info("[AppCore]: Loaded {} keyword(s) from '{}'", kws.size(), path);
    m_ranking.SetKeywords(std::move(kws));
}

bool AppCore::SaveKeywords(const std::vector<std::string>& keywords) {
    const std::string& path = m_config.get_keywords_file();
    if (path.empty())
        return false;

    std::string content;
    for (const auto& kw : keywords)
        content += kw + "\n";
    if (!WriteFileAtomically(path, content)) {
        spdlog::error("[AppCore]: Cannot write keywords file '{}'", path);
        return false;
    }

    m_ranking.SetKeywords(keywords);
    FetchArticles(); // re-rank the view on screen with the new keywords
    spdlog::info("[AppCore]: Saved {} keyword(s) to '{}'", keywords.size(), path);
    return true;
}

std::vector<std::string> AppCore::GetKeywords() const { return m_ranking.Keywords(); }

void AppCore::StartAutoRefresh() {
    int minutes = m_auto_refresh_minutes > 0 ? m_auto_refresh_minutes : 60;
    StartAutoRefresh(std::chrono::minutes(minutes));
}

void AppCore::StartAutoRefresh(std::chrono::milliseconds interval) {
    if (m_refresh_running.load())
        return;
    m_refresh_running.store(true);
    m_refresh_thread = std::thread([this, interval] {
        // Hold the mutex for the entire loop so that StopAutoRefresh() can
        // only write m_refresh_running while the mutex is free (i.e. while
        // the thread is either inside wait_for or fetching with the lock
        // temporarily released).  This prevents the lost-wakeup race where
        // notify_all() fires before wait_for() begins.
        std::unique_lock<std::mutex> lock(m_refresh_mutex);
        while (m_refresh_running.load()) {
            m_refresh_cv.wait_for(lock, interval, [this] { return !m_refresh_running.load(); });
            if (!m_refresh_running.load())
                break;
            lock.unlock();
            RefreshFromNetwork();
            lock.lock();
        }
    });
}

void AppCore::RefreshFromNetwork() {
    // Runs on the refresh thread. It only touches the fetcher and the DB
    // (which serialises access); the article list belongs to the UI thread,
    // which re-queries via TryRefetchIfNeeded once m_needs_refetch is set.
    if (m_fetching.exchange(true))
        return; // the initial fetch is still running
    try {
        auto articles = m_fetcher->Fetch();
        m_db->AddArticles(articles);
        m_db->SetMetadata("last_fetch_end_ts", std::to_string(now_ts()));
        m_last_fetch_ok.store(true);
        spdlog::info("[AppCore]: Auto-refresh stored {} article(s)", articles.size());
    } catch (const std::exception& e) {
        m_last_fetch_ok.store(false);
        spdlog::error("[AppCore]: Auto-refresh failed: {}", e.what());
    }
    m_fetching.store(false);
    m_needs_refetch.store(true);
}

void AppCore::StopAutoRefresh() {
    {
        // Acquire the mutex before writing the flag so the write is
        // synchronised with the thread's mutex-protected predicate check.
        std::lock_guard<std::mutex> lock(m_refresh_mutex);
        if (!m_refresh_running.load())
            return;
        m_refresh_running.store(false);
    }
    m_refresh_cv.notify_all();
    if (m_refresh_thread.joinable()) {
        m_refresh_thread.join();
    }
}

bool AppCore::IsAutoRefreshing() const { return m_refresh_running.load(); }

// ---------------------------------------------------------------------------
// Undo ring buffer
// ---------------------------------------------------------------------------

void AppCore::PushUndo(UndoEntry entry) {
    if (m_undo_capacity == 0)
        return;
    m_undo_buffer[m_undo_write] = std::move(entry);
    m_undo_write = (m_undo_write + 1) % m_undo_capacity;
    if (m_undo_count < m_undo_capacity)
        ++m_undo_count;
}

auto AppCore::PopUndo() -> std::optional<UndoEntry> {
    if (m_undo_count == 0)
        return std::nullopt;
    --m_undo_count;
    // Step the write head back one slot — that slot holds the most-recently pushed entry.
    m_undo_write = (m_undo_write + m_undo_capacity - 1) % m_undo_capacity;
    return std::move(m_undo_buffer[m_undo_write]);
}

bool AppCore::CanUndo() const { return m_undo_count > 0; }

void AppCore::UndoLastDelete() {
    auto entry = PopUndo();
    if (!entry)
        return;

    for (const auto& snap : *entry) {
        m_db->AddArticle(snap.article);
        if (snap.article.read)
            m_db->MarkArticleRead(snap.article.link);
        if (snap.rating != 0)
            m_db->SetRating(snap.article.link, snap.rating);
        for (const auto& [proj, note] : snap.project_notes) {
            m_db->LinkArticleToProject(snap.article.link, proj);
            if (!note.empty())
                m_db->SetProjectNote(proj, snap.article.link, note);
        }
        for (const auto& tag : snap.tags)
            m_db->LinkArticleToTag(snap.article.link, tag);
    }
    FetchArticles();
}

void AppCore::SetUndoCapacity(std::size_t capacity) {
    m_undo_capacity = capacity;
    m_undo_buffer.assign(capacity, UndoEntry{});
    m_undo_write = 0;
    m_undo_count = 0;
}

int AppCore::GetAutoRefreshMinutes() const { return m_auto_refresh_minutes; }

} // namespace Arxiv
