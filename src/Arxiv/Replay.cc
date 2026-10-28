// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Replay.hh"

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/HttpClient.hh"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string_view>
#include <unistd.h>
#include <unordered_map>

using json = nlohmann::json;

namespace {
// Text from the feed or typed by the user may not be valid UTF-8; dump()
// would throw (and end the program). Invalid bytes become U+FFFD instead.
std::string Dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }
} // namespace

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
constexpr std::string_view SetFilterIndex = "set_filter_index";
constexpr std::string_view SetArticleIndex = "set_article_index";
constexpr std::string_view ToggleBookmark = "toggle_bookmark";
constexpr std::string_view DownloadArticle = "download_article";
constexpr std::string_view AddProject = "add_project";
constexpr std::string_view RemoveProject = "remove_project";
constexpr std::string_view SetProjectParent = "set_project_parent";
constexpr std::string_view LinkArticleToProject = "link_article_to_project";
constexpr std::string_view UnlinkArticleFromProject = "unlink_article_from_project";
constexpr std::string_view SetDateRange = "set_date_range";
constexpr std::string_view SetSearchQuery = "set_search_query";
constexpr std::string_view SetProjectNote = "set_project_note";
constexpr std::string_view RateArticle = "rate_article";
constexpr std::string_view ForceRetrain = "force_retrain";
constexpr std::string_view ExportProjectMarkdown = "export_project_markdown";
constexpr std::string_view ExportProjectText = "export_project_text";
constexpr std::string_view ExportProjectJSON = "export_project_json";
constexpr std::string_view ImportProjectJSON = "import_project_json";
constexpr std::string_view ExportProjectBibTeX = "export_project_bibtex";
constexpr std::string_view ExportArticleBibTeX = "export_article_bibtex";
constexpr std::string_view ExportArticlesBibTeX = "export_articles_bibtex";
constexpr std::string_view ToggleSelection = "toggle_selection";
constexpr std::string_view ClearSelection = "clear_selection";
constexpr std::string_view RenameProject = "rename_project";
constexpr std::string_view RenameTag = "rename_tag";
constexpr std::string_view ExportSelectedDigest = "export_selected_digest";
constexpr std::string_view ExportToObsidian = "export_to_obsidian";
constexpr std::string_view ExportDailyDigest = "export_daily_digest";
constexpr std::string_view ToggleCategory = "toggle_category";
constexpr std::string_view SetActiveCategories = "set_active_categories";
constexpr std::string_view SaveKeywords = "save_keywords";
} // namespace ActionName

namespace {

using Handler = std::function<void(AppCore&, const json&)>;

const std::unordered_map<std::string, Handler>& dispatch_table() {
    static const std::unordered_map<std::string, Handler> table = {
        {std::string(ActionName::SetFilterIndex),
         [](AppCore& c, const json& j) { c.SetFilterIndex(j.value("index", 0)); }},
        {std::string(ActionName::SetArticleIndex),
         [](AppCore& c, const json& j) { c.SetArticleIndex(j.value("index", 0)); }},
        {std::string(ActionName::ToggleBookmark),
         [](AppCore& c, const json& j) { c.ToggleBookmark(j.value("article_link", "")); }},
        {std::string(ActionName::DownloadArticle),
         [](AppCore& c, const json& j) { c.DownloadArticle(j.value("article_id", "")); }},
        {std::string(ActionName::AddProject),
         [](AppCore& c, const json& j) { c.AddProject(j.value("name", "")); }},
        {std::string(ActionName::RemoveProject),
         [](AppCore& c, const json& j) { c.RemoveProject(j.value("name", "")); }},
        {std::string(ActionName::SetProjectParent),
         [](AppCore& c, const json& j) {
             c.SetProjectParent(j.value("project", ""), j.value("parent", ""));
         }},
        {std::string(ActionName::LinkArticleToProject),
         [](AppCore& c, const json& j) {
             c.LinkArticleToProject(j.value("article_link", ""), j.value("project", ""));
         }},
        {std::string(ActionName::UnlinkArticleFromProject),
         [](AppCore& c, const json& j) {
             c.UnlinkArticleFromProject(j.value("article_link", ""), j.value("project", ""));
         }},
        {std::string(ActionName::SetDateRange),
         [](AppCore& c, const json& j) {
             c.SetDateRange(j.value("start", ""), j.value("end", ""));
         }},
        {std::string(ActionName::SetSearchQuery),
         [](AppCore& c, const json& j) {
             c.SetSearchQuery(j.value("query", ""),
                              j.value("title", true),
                              j.value("authors", true),
                              j.value("abstract_field", true));
         }},
        {std::string(ActionName::SetProjectNote),
         [](AppCore& c, const json& j) {
             c.SetProjectNote(
                 j.value("project", ""), j.value("article_link", ""), j.value("note", ""));
         }},
        {std::string(ActionName::RateArticle),
         [](AppCore& c, const json& j) {
             c.RateArticle(j.value("article_link", ""), j.value("rating", 0));
         }},
        {std::string(ActionName::ForceRetrain), [](AppCore& c, const json&) { c.ForceRetrain(); }},
        {std::string(ActionName::ExportProjectMarkdown),
         [](AppCore& c, const json& j) {
             c.ExportProjectMarkdown(j.value("project", ""), j.value("path", ""));
         }},
        {std::string(ActionName::ExportProjectText),
         [](AppCore& c, const json& j) {
             c.ExportProjectText(j.value("project", ""), j.value("path", ""));
         }},
        {std::string(ActionName::ExportProjectJSON),
         [](AppCore& c, const json& j) {
             c.ExportProjectJSON(j.value("project", ""), j.value("path", ""));
         }},
        {std::string(ActionName::ImportProjectJSON),
         [](AppCore& c, const json& j) { c.ImportProjectJSON(j.value("path", "")); }},
        {std::string(ActionName::ExportProjectBibTeX),
         [](AppCore& c, const json& j) {
             c.ExportProjectBibTeX(j.value("project", ""), j.value("path", ""));
         }},
        {std::string(ActionName::ExportArticleBibTeX),
         [](AppCore& c, const json& j) {
             const std::string link = j.value("article_link", "");
             const std::string path = j.value("path", "");
             for (const auto& a : c.GetCurrentArticles()) {
                 if (a.link == link) {
                     c.ExportArticleBibTeX(a, path);
                     break;
                 }
             }
         }},
        {std::string(ActionName::ExportArticlesBibTeX),
         [](AppCore& c, const json& j) {
             c.ExportArticlesBibTeX(c.GetCurrentArticles(), j.value("path", ""));
         }},
        {std::string(ActionName::ToggleSelection),
         [](AppCore& c, const json& j) { c.ToggleSelection(j.value("article_link", "")); }},
        {std::string(ActionName::ClearSelection),
         [](AppCore& c, const json&) { c.ClearSelections(); }},
        {std::string(ActionName::RenameProject),
         [](AppCore& c, const json& j) {
             c.RenameProject(j.value("from", ""), j.value("to", ""));
         }},
        {std::string(ActionName::RenameTag),
         [](AppCore& c, const json& j) { c.RenameTag(j.value("from", ""), j.value("to", "")); }},
        {std::string(ActionName::ExportSelectedDigest),
         [](AppCore& c, const json&) { c.ExportSelectedDigest(); }},
        {std::string(ActionName::ExportToObsidian),
         [](AppCore& c, const json&) { c.ExportSelectedToObsidian(); }},
        {std::string(ActionName::ExportDailyDigest),
         [](AppCore& c, const json& j) { c.ExportDailyDigest(j.value("path", "")); }},
        {std::string(ActionName::ToggleCategory),
         [](AppCore& c, const json& j) { c.ToggleCategory(j.value("category", "")); }},
        {std::string(ActionName::SetActiveCategories),
         [](AppCore& c, const json& j) {
             std::set<std::string> cats;
             if (j.contains("categories") && j["categories"].is_array()) {
                 for (const auto& cat : j["categories"])
                     cats.insert(cat.get<std::string>());
             }
             c.SetActiveCategories(cats);
         }},
        {std::string(ActionName::SaveKeywords),
         [](AppCore& c, const json& j) {
             std::vector<std::string> kws;
             if (j.contains("keywords") && j["keywords"].is_array()) {
                 for (const auto& kw : j["keywords"])
                     kws.push_back(kw.get<std::string>());
             }
             c.SaveKeywords(kws);
         }},
    };
    return table;
}

// Actions that read or write files outside the database, or download.
bool touches_files(const std::string& action) {
    static const std::set<std::string_view> file_actions = {
        ActionName::DownloadArticle,
        ActionName::ExportProjectMarkdown,
        ActionName::ExportProjectText,
        ActionName::ExportProjectJSON,
        ActionName::ImportProjectJSON,
        ActionName::ExportProjectBibTeX,
        ActionName::ExportArticleBibTeX,
        ActionName::ExportArticlesBibTeX,
        ActionName::ExportSelectedDigest,
        ActionName::ExportToObsidian,
        ActionName::ExportDailyDigest,
        ActionName::SaveKeywords,
    };
    return file_actions.count(action) != 0;
}

} // namespace

// ---------------------------------------------------------------------------
// ReplayRecorder
// ---------------------------------------------------------------------------

ReplayRecorder::ReplayRecorder() = default;

namespace {
// Keep one previous log; the current one starts over once it is too big.
const std::string& rotate_if_large(const std::string& path, std::uintmax_t max_bytes) {
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) > max_bytes && !ec)
        std::filesystem::rename(path, path + ".1", ec);
    return path;
}
} // namespace

ReplayRecorder::ReplayRecorder(const std::string& file_path)
    // The log holds searches, notes, keywords and paths: only the user may
    // read it (0600), and programs we start must not inherit it.
    : m_fd(::open(rotate_if_large(file_path, kMaxFileBytes).c_str(),
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
                  0600)) {
    // If the file cannot be opened the recorder stays in-memory only.
    if (m_fd >= 0)
        m_file_path = file_path;
}

ReplayRecorder::~ReplayRecorder() {
    if (m_fd >= 0)
        ::close(m_fd);
}

long long ReplayRecorder::NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void ReplayRecorder::Record(const std::string& json_line) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.push_back(json_line);
    if (m_entries.size() > static_cast<std::size_t>(kMaxEntries))
        m_entries.pop_front();
    if (m_fd >= 0) {
        const std::string line = json_line + '\n';
        const char* data = line.data();
        std::size_t left = line.size();
        while (left > 0) {
            const ssize_t n = ::write(m_fd, data, left);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break; // keep the in-memory copy; the file is best effort
            data += n;
            left -= static_cast<std::size_t>(n);
        }
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
    Record(Dump(j));
}

void ReplayRecorder::RecordSetArticleIndex(int index) {
    auto j = make_entry(ActionName::SetArticleIndex, NowMs());
    j["index"] = index;
    Record(Dump(j));
}

void ReplayRecorder::RecordToggleBookmark(const std::string& article_link) {
    auto j = make_entry(ActionName::ToggleBookmark, NowMs());
    j["article_link"] = article_link;
    Record(Dump(j));
}

void ReplayRecorder::RecordDownloadArticle(const std::string& article_id) {
    auto j = make_entry(ActionName::DownloadArticle, NowMs());
    j["article_id"] = article_id;
    Record(Dump(j));
}

void ReplayRecorder::RecordAddProject(const std::string& name) {
    auto j = make_entry(ActionName::AddProject, NowMs());
    j["name"] = name;
    Record(Dump(j));
}

void ReplayRecorder::RecordRemoveProject(const std::string& name) {
    auto j = make_entry(ActionName::RemoveProject, NowMs());
    j["name"] = name;
    Record(Dump(j));
}

void ReplayRecorder::RecordSetProjectParent(const std::string& project, const std::string& parent) {
    auto j = make_entry(ActionName::SetProjectParent, NowMs());
    j["project"] = project;
    j["parent"] = parent;
    Record(Dump(j));
}

void ReplayRecorder::RecordLinkArticleToProject(const std::string& article_link,
                                                const std::string& project) {
    auto j = make_entry(ActionName::LinkArticleToProject, NowMs());
    j["article_link"] = article_link;
    j["project"] = project;
    Record(Dump(j));
}

void ReplayRecorder::RecordUnlinkArticleFromProject(const std::string& article_link,
                                                    const std::string& project) {
    auto j = make_entry(ActionName::UnlinkArticleFromProject, NowMs());
    j["article_link"] = article_link;
    j["project"] = project;
    Record(Dump(j));
}

void ReplayRecorder::RecordSetDateRange(const std::string& start, const std::string& end) {
    auto j = make_entry(ActionName::SetDateRange, NowMs());
    j["start"] = start;
    j["end"] = end;
    Record(Dump(j));
}

void ReplayRecorder::RecordSetSearchQuery(const std::string& query,
                                          bool title,
                                          bool authors,
                                          bool abstract_field) {
    auto j = make_entry(ActionName::SetSearchQuery, NowMs());
    j["query"] = query;
    j["title"] = title;
    j["authors"] = authors;
    j["abstract_field"] = abstract_field;
    Record(Dump(j));
}

void ReplayRecorder::RecordSetProjectNote(const std::string& project,
                                          const std::string& article_link,
                                          const std::string& note) {
    auto j = make_entry(ActionName::SetProjectNote, NowMs());
    j["project"] = project;
    j["article_link"] = article_link;
    j["note"] = note;
    Record(Dump(j));
}

void ReplayRecorder::RecordRateArticle(const std::string& article_link, int rating) {
    auto j = make_entry(ActionName::RateArticle, NowMs());
    j["article_link"] = article_link;
    j["rating"] = rating;
    Record(Dump(j));
}

void ReplayRecorder::RecordForceRetrain() {
    Record(Dump(make_entry(ActionName::ForceRetrain, NowMs())));
}

void ReplayRecorder::RecordExportProjectMarkdown(const std::string& project,
                                                 const std::string& path) {
    auto j = make_entry(ActionName::ExportProjectMarkdown, NowMs());
    j["project"] = project;
    j["path"] = path;
    Record(Dump(j));
}

void ReplayRecorder::RecordExportProjectText(const std::string& project, const std::string& path) {
    auto j = make_entry(ActionName::ExportProjectText, NowMs());
    j["project"] = project;
    j["path"] = path;
    Record(Dump(j));
}

void ReplayRecorder::RecordExportProjectJSON(const std::string& project, const std::string& path) {
    auto j = make_entry(ActionName::ExportProjectJSON, NowMs());
    j["project"] = project;
    j["path"] = path;
    Record(Dump(j));
}

void ReplayRecorder::RecordImportProjectJSON(const std::string& path) {
    auto j = make_entry(ActionName::ImportProjectJSON, NowMs());
    j["path"] = path;
    Record(Dump(j));
}

void ReplayRecorder::RecordExportArticleBibTeX(const std::string& article_link,
                                               const std::string& path) {
    auto j = make_entry(ActionName::ExportArticleBibTeX, NowMs());
    j["article_link"] = article_link;
    j["path"] = path;
    Record(Dump(j));
}

void ReplayRecorder::RecordExportArticlesBibTeX(const std::string& path) {
    auto j = make_entry(ActionName::ExportArticlesBibTeX, NowMs());
    j["path"] = path;
    Record(Dump(j));
}

void ReplayRecorder::RecordExportProjectBibTeX(const std::string& project,
                                               const std::string& path) {
    auto j = make_entry(ActionName::ExportProjectBibTeX, NowMs());
    j["project"] = project;
    j["path"] = path;
    Record(Dump(j));
}

void ReplayRecorder::RecordRenameProject(const std::string& from, const std::string& to) {
    auto j = make_entry(ActionName::RenameProject, NowMs());
    j["from"] = from;
    j["to"] = to;
    Record(Dump(j));
}

void ReplayRecorder::RecordRenameTag(const std::string& from, const std::string& to) {
    auto j = make_entry(ActionName::RenameTag, NowMs());
    j["from"] = from;
    j["to"] = to;
    Record(Dump(j));
}

void ReplayRecorder::RecordClearSelection() {
    Record(Dump(make_entry(ActionName::ClearSelection, NowMs())));
}

void ReplayRecorder::RecordToggleSelection(const std::string& article_link) {
    auto j = make_entry(ActionName::ToggleSelection, NowMs());
    j["article_link"] = article_link;
    Record(Dump(j));
}

void ReplayRecorder::RecordExportSelectedDigest(const std::string& path) {
    auto j = make_entry(ActionName::ExportSelectedDigest, NowMs());
    j["path"] = path;
    Record(Dump(j));
}

void ReplayRecorder::RecordExportToObsidian(const std::string& path) {
    auto j = make_entry(ActionName::ExportToObsidian, NowMs());
    j["path"] = path;
    Record(Dump(j));
}

void ReplayRecorder::RecordExportDailyDigest(const std::string& path) {
    auto j = make_entry(ActionName::ExportDailyDigest, NowMs());
    j["path"] = path;
    Record(Dump(j));
}

void ReplayRecorder::RecordToggleCategory(const std::string& category) {
    auto j = make_entry(ActionName::ToggleCategory, NowMs());
    j["category"] = category;
    Record(Dump(j));
}

void ReplayRecorder::RecordSetActiveCategories(const std::vector<std::string>& categories) {
    auto j = make_entry(ActionName::SetActiveCategories, NowMs());
    j["categories"] = categories;
    Record(Dump(j));
}

void ReplayRecorder::RecordSaveKeywords(const std::vector<std::string>& keywords) {
    auto j = make_entry(ActionName::SaveKeywords, NowMs());
    j["keywords"] = keywords;
    Record(Dump(j));
}

void ReplayRecorder::RecordEvent(const std::string& name, const std::string& detail) {
    json j{{"ts", NowMs()}, {"action", "event"}, {"name", name}};
    if (!detail.empty())
        j["detail"] = detail;
    Record(Dump(j));
}

// ---------------------------------------------------------------------------
// ReplayPlayer
// ---------------------------------------------------------------------------

bool ReplayPlayer::DispatchAction(const std::string& json_line,
                                  AppCore& core,
                                  const Options& options,
                                  std::string& error_out) {

    json j;
    try {
        j = json::parse(json_line);
    } catch (const json::exception& e) { // parse_error, or out_of_range (1e70000)
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
    if (!options.file_actions && touches_files(it->first))
        return false; // skipped, like an unknown action
    // Handlers read fields with json::value(), which throws type_error when a
    // field has the wrong type; a malformed line is an error, not a crash.
    try {
        it->second(core, j);
    } catch (const json::exception& e) {
        error_out = std::string("Malformed '") + it->first + "' action: " + e.what();
        return false;
    }
    return true;
}

ReplayPlayer::Result ReplayPlayer::FromString(const std::string& jsonl, AppCore& core) {
    return FromString(jsonl, core, Options{});
}

ReplayPlayer::Result
ReplayPlayer::FromString(const std::string& text, AppCore& core, const Options& options) {
    return Run(ReplayLog::Parse(text), core, options);
}

ReplayPlayer::Result ReplayPlayer::FromFile(const std::string& path, AppCore& core) {
    return FromFile(path, core, Options{});
}

ReplayPlayer::Result
ReplayPlayer::FromFile(const std::string& path, AppCore& core, const Options& options) {
    std::ifstream f(path);
    if (!f.is_open()) {
        Result r;
        r.error = "Cannot open file: " + path;
        return r;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return FromString(ss.str(), core, options);
}

namespace {

constexpr std::string_view kLegacyBanner = "=== arxiv-tui Crash Report ===";

// Header fields are shown on the terminal by --replay, and a report may come
// from anyone: keep printable text only (no escape sequences).
std::string printable(std::string s) {
    s.erase(std::remove_if(s.begin(),
                           s.end(),
                           [](char c) {
                               const auto u = static_cast<unsigned char>(c);
                               return u < 0x20 || u == 0x7f;
                           }),
            s.end());
    return s;
}

// The first line of a report written since the JSON header was added.
std::optional<CrashInfo> CrashHeaderOf(const std::string& line) {
    if (line.empty() || line[0] != '{')
        return std::nullopt;
    const json j = json::parse(line, nullptr, false);
    if (!j.is_object() || !j.contains("arxiv_tui_crash_report"))
        return std::nullopt;
    CrashInfo info;
    try {
        info.version = printable(j.value("version", ""));
        info.signal = printable(j.value("signal", ""));
        info.signum = j.value("signum", 0);
        info.time = printable(j.value("time", ""));
        info.pid = j.value("pid", 0LL);
        info.replay_log = printable(j.value("replay_log", ""));
    } catch (const json::exception&) {
        // A field of the wrong type: keep what was read.
    }
    return info;
}

// Reports from before the header: "Signal    : SIGSEGV (11)".
CrashInfo LegacyCrashInfo(std::istringstream& in) {
    CrashInfo info;
    std::string line;
    while (std::getline(in, line) && line.rfind("--- ", 0) != 0) {
        if (line.rfind("Timestamp : ", 0) == 0)
            info.time = printable(line.substr(12));
        if (line.rfind("Signal    : ", 0) == 0) {
            std::istringstream fields(line.substr(12));
            char paren = 0;
            fields >> info.signal >> paren >> info.signum;
            info.signal = printable(info.signal);
        }
    }
    return info;
}

// The sandbox's network: every request fails, as if offline. A replay must
// not reach InspireHEP or arXiv (linking a paper looks up its BibTeX).
class NoNetwork : public HttpClient {
  public:
    HttpResponse Get(const std::string&, std::chrono::milliseconds) override {
        return {0, "", "network disabled during replay"};
    }
    HttpResponse Download(const std::string&,
                          std::chrono::milliseconds,
                          const std::function<bool(std::string_view)>&) override {
        return {0, "", "network disabled during replay"};
    }
};

} // namespace

ReplayLog ReplayLog::Parse(const std::string& text) {
    ReplayLog log;
    std::istringstream in(text);
    std::string line;
    std::getline(in, line);
    if (auto header = CrashHeaderOf(line))
        log.crash = std::move(header);
    else if (line.rfind(kLegacyBanner, 0) == 0)
        log.crash = LegacyCrashInfo(in);

    if (!log.crash) {
        // A replay log: every line is an entry.
        do {
            if (!line.empty())
                log.entries.push_back(line);
        } while (std::getline(in, line));
        return log;
    }
    // A crash report: the entries are its Replay Log section.
    bool inside = false;
    while (std::getline(in, line)) {
        if (!inside) {
            inside = line.rfind("--- Replay Log", 0) == 0;
            continue;
        }
        if (line.rfind("=== End of Report", 0) == 0)
            break;
        if (!line.empty() && line != "(earlier entries omitted)")
            log.entries.push_back(line);
    }
    return log;
}

ReplayPlayer::Result
ReplayPlayer::Run(const ReplayLog& log, AppCore& core, const Options& options) {
    Result result;
    result.crash = log.crash;
    for (const auto& entry : log.entries) {
        ++result.total;
        std::string err;
        if (DispatchAction(entry, core, options, err)) {
            ++result.replayed;
        } else if (err.empty()) {
            ++result.skipped; // unknown action, diagnostic event or file action
        } else {
            result.error = err; // stop on the first malformed entry
            return result;
        }
    }
    return result;
}

ReplayPlayer::Result ReplayPlayer::RunSandboxed(const Config& config, const std::string& path) {
    return RunSandboxed(config, path, Options{});
}

ReplayPlayer::Result
ReplayPlayer::RunSandboxed(const Config& config, const std::string& path, const Options& options) {
    namespace fs = std::filesystem;
    Result result;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.error = "Cannot open file: " + path;
        return result;
    }
    std::stringstream text;
    text << in.rdbuf();
    const ReplayLog log = ReplayLog::Parse(text.str());

    // A private scratch directory (mkdtemp: unpredictable name, mode 0700)
    // for copies of the ranker and keywords and for any file action.
    std::string templ = (fs::temp_directory_path() / "arxiv-tui-replay-XXXXXX").string();
    if (!::mkdtemp(templ.data())) {
        result.error = "Cannot create a scratch directory for the replay";
        result.crash = log.crash;
        return result;
    }
    const fs::path scratch = templ;
    std::error_code ignore;
    Config sandboxed = config;
    for (auto [from, to] : {std::pair{config.get_ranker_file(), scratch / "ranker.bin"},
                            std::pair{config.get_keywords_file(), scratch / "keywords.txt"}})
        if (!from.empty())
            fs::copy_file(from, to, fs::copy_options::overwrite_existing, ignore);
    sandboxed.set_ranker_file((scratch / "ranker.bin").string());
    sandboxed.set_keywords_file((scratch / "keywords.txt").string());
    sandboxed.set_download_dir((scratch / "downloads").string());
    sandboxed.set_export_dir((scratch / "exports").string());

    try {
        auto db = DatabaseManager::InMemoryCopyOf(config.get_db_file());
        // The copy still names the user's exported project .bib files, which
        // linking a paper appends to: detach them.
        for (const auto& project : db->GetProjects())
            db->SetProjectBibPath(project, "");
        AppCore core(sandboxed,
                     std::move(db),
                     std::make_unique<Fetcher>(sandboxed.get_topics(),
                                               sandboxed.get_download_dir(),
                                               std::make_unique<NoNetwork>()),
                     AppCore::FetchMode::Offline);
        result = Run(log, core, options);
    } catch (const std::exception& e) {
        result.error = e.what();
        result.crash = log.crash;
    }
    fs::remove_all(scratch, ignore);
    return result;
}

} // namespace Arxiv
