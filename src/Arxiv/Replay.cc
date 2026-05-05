#include "Arxiv/Replay.hh"
#include "Arxiv/AppCore.hh"

#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Arxiv {

// ---------------------------------------------------------------------------
// Action registry
//
// Every replayable action is declared exactly once below: its on-the-wire
// name (ActionName::*) and the dispatcher that applies it to an AppCore
// (dispatch_table). The recorder helpers and the player both consult these,
// so adding a new action only requires (1) appending an ActionName constant,
// (2) adding a dispatch_table entry, and (3) adding a Record* helper that
// references the same constant.
// ---------------------------------------------------------------------------

namespace ActionName {
constexpr std::string_view SetFilterIndex           = "set_filter_index";
constexpr std::string_view SetArticleIndex          = "set_article_index";
constexpr std::string_view ToggleBookmark           = "toggle_bookmark";
constexpr std::string_view DownloadArticle          = "download_article";
constexpr std::string_view AddProject               = "add_project";
constexpr std::string_view RemoveProject            = "remove_project";
constexpr std::string_view SetProjectParent         = "set_project_parent";
constexpr std::string_view LinkArticleToProject     = "link_article_to_project";
constexpr std::string_view UnlinkArticleFromProject = "unlink_article_from_project";
constexpr std::string_view SetDateRange             = "set_date_range";
constexpr std::string_view SetSearchQuery           = "set_search_query";
constexpr std::string_view SetProjectNote           = "set_project_note";
constexpr std::string_view RateArticle              = "rate_article";
constexpr std::string_view ForceRetrain             = "force_retrain";
constexpr std::string_view ExportProjectMarkdown    = "export_project_markdown";
constexpr std::string_view ExportProjectText        = "export_project_text";
constexpr std::string_view ExportProjectJSON        = "export_project_json";
constexpr std::string_view ImportProjectJSON        = "import_project_json";
constexpr std::string_view ExportProjectBibTeX      = "export_project_bibtex";
constexpr std::string_view ExportArticleBibTeX      = "export_article_bibtex";
constexpr std::string_view ExportArticlesBibTeX     = "export_articles_bibtex";
} // namespace ActionName

namespace {

using Handler = std::function<void(AppCore&, const json&)>;

const std::unordered_map<std::string, Handler>& dispatch_table() {
    static const std::unordered_map<std::string, Handler> table = {
        {std::string(ActionName::SetFilterIndex), [](AppCore& c, const json& j) {
            c.SetFilterIndex(j.value("index", 0));
        }},
        {std::string(ActionName::SetArticleIndex), [](AppCore& c, const json& j) {
            c.SetArticleIndex(j.value("index", 0));
        }},
        {std::string(ActionName::ToggleBookmark), [](AppCore& c, const json& j) {
            c.ToggleBookmark(j.value("article_link", ""));
        }},
        {std::string(ActionName::DownloadArticle), [](AppCore& c, const json& j) {
            c.DownloadArticle(j.value("article_id", ""));
        }},
        {std::string(ActionName::AddProject), [](AppCore& c, const json& j) {
            c.AddProject(j.value("name", ""));
        }},
        {std::string(ActionName::RemoveProject), [](AppCore& c, const json& j) {
            c.RemoveProject(j.value("name", ""));
        }},
        {std::string(ActionName::SetProjectParent), [](AppCore& c, const json& j) {
            c.SetProjectParent(j.value("project", ""), j.value("parent", ""));
        }},
        {std::string(ActionName::LinkArticleToProject), [](AppCore& c, const json& j) {
            c.LinkArticleToProject(j.value("article_link", ""), j.value("project", ""));
        }},
        {std::string(ActionName::UnlinkArticleFromProject), [](AppCore& c, const json& j) {
            c.UnlinkArticleFromProject(j.value("article_link", ""), j.value("project", ""));
        }},
        {std::string(ActionName::SetDateRange), [](AppCore& c, const json& j) {
            c.SetDateRange(j.value("start", ""), j.value("end", ""));
        }},
        {std::string(ActionName::SetSearchQuery), [](AppCore& c, const json& j) {
            c.SetSearchQuery(
                j.value("query", ""),
                j.value("title", true),
                j.value("authors", true),
                j.value("abstract_field", true));
        }},
        {std::string(ActionName::SetProjectNote), [](AppCore& c, const json& j) {
            c.SetProjectNote(j.value("project", ""), j.value("article_link", ""), j.value("note", ""));
        }},
        {std::string(ActionName::RateArticle), [](AppCore& c, const json& j) {
            c.RateArticle(j.value("article_link", ""), j.value("rating", 0));
        }},
        {std::string(ActionName::ForceRetrain), [](AppCore& c, const json&) {
            c.ForceRetrain();
        }},
        {std::string(ActionName::ExportProjectMarkdown), [](AppCore& c, const json& j) {
            c.ExportProjectMarkdown(j.value("project", ""), j.value("path", ""));
        }},
        {std::string(ActionName::ExportProjectText), [](AppCore& c, const json& j) {
            c.ExportProjectText(j.value("project", ""), j.value("path", ""));
        }},
        {std::string(ActionName::ExportProjectJSON), [](AppCore& c, const json& j) {
            c.ExportProjectJSON(j.value("project", ""), j.value("path", ""));
        }},
        {std::string(ActionName::ImportProjectJSON), [](AppCore& c, const json& j) {
            c.ImportProjectJSON(j.value("path", ""));
        }},
        {std::string(ActionName::ExportProjectBibTeX), [](AppCore& c, const json& j) {
            c.ExportProjectBibTeX(j.value("project", ""), j.value("path", ""));
        }},
        {std::string(ActionName::ExportArticleBibTeX), [](AppCore& c, const json& j) {
            const std::string link = j.value("article_link", "");
            const std::string path = j.value("path", "");
            for (const auto& a : c.GetCurrentArticles()) {
                if (a.link == link) {
                    c.ExportArticleBibTeX(a, path);
                    break;
                }
            }
        }},
        {std::string(ActionName::ExportArticlesBibTeX), [](AppCore& c, const json& j) {
            c.ExportArticlesBibTeX(c.GetCurrentArticles(), j.value("path", ""));
        }},
    };
    return table;
}

} // namespace

// ---------------------------------------------------------------------------
// ReplayRecorder
// ---------------------------------------------------------------------------

ReplayRecorder::ReplayRecorder() = default;

ReplayRecorder::ReplayRecorder(const std::string& file_path)
    : m_file(file_path, std::ios::out | std::ios::app)
{
    if (!m_file.is_open()) {
        // Non-fatal: fall back to in-memory only
    }
}

ReplayRecorder::~ReplayRecorder() {
    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
}

long long ReplayRecorder::NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

void ReplayRecorder::Record(const std::string& json_line) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.push_back(json_line);
    if (m_file.is_open()) {
        m_file << json_line << '\n';
        m_file.flush();
    }
}

std::string ReplayRecorder::GetJSONL() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string result;
    for (const auto& e : m_entries) {
        result += e;
        result += '\n';
    }
    return result;
}

std::size_t ReplayRecorder::GetCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

// --- Record methods ---
//
// Each helper builds the action's JSON object using an ActionName constant so
// the on-the-wire name has a single source of truth shared with the
// dispatch_table above.

namespace {
/// Build a base entry with the common {ts, action} envelope. Caller adds
/// any action-specific fields before serialising.
json make_entry(std::string_view action, long long ts) {
    return json{{"ts", ts}, {"action", action}};
}
} // namespace

void ReplayRecorder::RecordSetFilterIndex(int index) {
    auto j = make_entry(ActionName::SetFilterIndex, NowMs());
    j["index"] = index;
    Record(j.dump());
}

void ReplayRecorder::RecordSetArticleIndex(int index) {
    auto j = make_entry(ActionName::SetArticleIndex, NowMs());
    j["index"] = index;
    Record(j.dump());
}

void ReplayRecorder::RecordToggleBookmark(const std::string& article_link) {
    auto j = make_entry(ActionName::ToggleBookmark, NowMs());
    j["article_link"] = article_link;
    Record(j.dump());
}

void ReplayRecorder::RecordDownloadArticle(const std::string& article_id) {
    auto j = make_entry(ActionName::DownloadArticle, NowMs());
    j["article_id"] = article_id;
    Record(j.dump());
}

void ReplayRecorder::RecordAddProject(const std::string& name) {
    auto j = make_entry(ActionName::AddProject, NowMs());
    j["name"] = name;
    Record(j.dump());
}

void ReplayRecorder::RecordRemoveProject(const std::string& name) {
    auto j = make_entry(ActionName::RemoveProject, NowMs());
    j["name"] = name;
    Record(j.dump());
}

void ReplayRecorder::RecordSetProjectParent(const std::string& project, const std::string& parent) {
    auto j = make_entry(ActionName::SetProjectParent, NowMs());
    j["project"] = project;
    j["parent"]  = parent;
    Record(j.dump());
}

void ReplayRecorder::RecordLinkArticleToProject(const std::string& article_link, const std::string& project) {
    auto j = make_entry(ActionName::LinkArticleToProject, NowMs());
    j["article_link"] = article_link;
    j["project"]      = project;
    Record(j.dump());
}

void ReplayRecorder::RecordUnlinkArticleFromProject(const std::string& article_link, const std::string& project) {
    auto j = make_entry(ActionName::UnlinkArticleFromProject, NowMs());
    j["article_link"] = article_link;
    j["project"]      = project;
    Record(j.dump());
}

void ReplayRecorder::RecordSetDateRange(const std::string& start, const std::string& end) {
    auto j = make_entry(ActionName::SetDateRange, NowMs());
    j["start"] = start;
    j["end"]   = end;
    Record(j.dump());
}

void ReplayRecorder::RecordSetSearchQuery(const std::string& query, bool title, bool authors, bool abstract_field) {
    auto j = make_entry(ActionName::SetSearchQuery, NowMs());
    j["query"]          = query;
    j["title"]          = title;
    j["authors"]        = authors;
    j["abstract_field"] = abstract_field;
    Record(j.dump());
}

void ReplayRecorder::RecordSetProjectNote(const std::string& project, const std::string& article_link, const std::string& note) {
    auto j = make_entry(ActionName::SetProjectNote, NowMs());
    j["project"]      = project;
    j["article_link"] = article_link;
    j["note"]         = note;
    Record(j.dump());
}

void ReplayRecorder::RecordRateArticle(const std::string& article_link, int rating) {
    auto j = make_entry(ActionName::RateArticle, NowMs());
    j["article_link"] = article_link;
    j["rating"]       = rating;
    Record(j.dump());
}

void ReplayRecorder::RecordForceRetrain() {
    Record(make_entry(ActionName::ForceRetrain, NowMs()).dump());
}

void ReplayRecorder::RecordExportProjectMarkdown(const std::string& project, const std::string& path) {
    auto j = make_entry(ActionName::ExportProjectMarkdown, NowMs());
    j["project"] = project;
    j["path"]    = path;
    Record(j.dump());
}

void ReplayRecorder::RecordExportProjectText(const std::string& project, const std::string& path) {
    auto j = make_entry(ActionName::ExportProjectText, NowMs());
    j["project"] = project;
    j["path"]    = path;
    Record(j.dump());
}

void ReplayRecorder::RecordExportProjectJSON(const std::string& project, const std::string& path) {
    auto j = make_entry(ActionName::ExportProjectJSON, NowMs());
    j["project"] = project;
    j["path"]    = path;
    Record(j.dump());
}

void ReplayRecorder::RecordImportProjectJSON(const std::string& path) {
    auto j = make_entry(ActionName::ImportProjectJSON, NowMs());
    j["path"] = path;
    Record(j.dump());
}

void ReplayRecorder::RecordExportArticleBibTeX(const std::string& article_link, const std::string& path) {
    auto j = make_entry(ActionName::ExportArticleBibTeX, NowMs());
    j["article_link"] = article_link;
    j["path"]         = path;
    Record(j.dump());
}

void ReplayRecorder::RecordExportArticlesBibTeX(const std::string& path) {
    auto j = make_entry(ActionName::ExportArticlesBibTeX, NowMs());
    j["path"] = path;
    Record(j.dump());
}

void ReplayRecorder::RecordExportProjectBibTeX(const std::string& project, const std::string& path) {
    auto j = make_entry(ActionName::ExportProjectBibTeX, NowMs());
    j["project"] = project;
    j["path"]    = path;
    Record(j.dump());
}

// ---------------------------------------------------------------------------
// ReplayPlayer
// ---------------------------------------------------------------------------

bool ReplayPlayer::DispatchAction(const std::string& json_line, AppCore& core, std::string& error_out) {
    json j;
    try {
        j = json::parse(json_line);
    } catch (const json::parse_error& e) {
        error_out = std::string("JSON parse error: ") + e.what();
        return false;
    }

    if (!j.contains("action") || !j["action"].is_string()) {
        error_out = "Missing or invalid 'action' field";
        return false;
    }

    const auto& table = dispatch_table();
    auto it = table.find(j["action"].get<std::string>());
    if (it == table.end()) {
        // Unknown action — caller treats this as skipped, not error
        return false;
    }
    it->second(core, j);
    return true;
}

ReplayPlayer::Result ReplayPlayer::FromString(const std::string& jsonl, AppCore& core) {
    Result result;
    if (jsonl.empty()) return result;

    std::istringstream stream(jsonl);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        ++result.total;
        std::string err;
        if (!DispatchAction(line, core, err)) {
            if (!err.empty()) {
                // Parse error or structural error
                result.error = err;
                return result; // stop on first hard error
            } else {
                ++result.skipped;
            }
        } else {
            ++result.replayed;
        }
    }
    return result;
}

ReplayPlayer::Result ReplayPlayer::FromFile(const std::string& path, AppCore& core) {
    std::ifstream f(path);
    if (!f.is_open()) {
        Result r;
        r.error = "Cannot open file: " + path;
        return r;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return FromString(ss.str(), core);
}

} // namespace Arxiv
