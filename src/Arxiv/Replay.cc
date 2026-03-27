#include "Arxiv/Replay.hh"
#include "Arxiv/AppCore.hh"

#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Arxiv {

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

void ReplayRecorder::RecordSetFilterIndex(int index) {
    json j = {{"ts", NowMs()}, {"action", "set_filter_index"}, {"index", index}};
    Record(j.dump());
}

void ReplayRecorder::RecordSetArticleIndex(int index) {
    json j = {{"ts", NowMs()}, {"action", "set_article_index"}, {"index", index}};
    Record(j.dump());
}

void ReplayRecorder::RecordToggleBookmark(const std::string& article_link) {
    json j = {{"ts", NowMs()}, {"action", "toggle_bookmark"}, {"article_link", article_link}};
    Record(j.dump());
}

void ReplayRecorder::RecordDownloadArticle(const std::string& article_id) {
    json j = {{"ts", NowMs()}, {"action", "download_article"}, {"article_id", article_id}};
    Record(j.dump());
}

void ReplayRecorder::RecordAddProject(const std::string& name) {
    json j = {{"ts", NowMs()}, {"action", "add_project"}, {"name", name}};
    Record(j.dump());
}

void ReplayRecorder::RecordRemoveProject(const std::string& name) {
    json j = {{"ts", NowMs()}, {"action", "remove_project"}, {"name", name}};
    Record(j.dump());
}

void ReplayRecorder::RecordSetProjectParent(const std::string& project, const std::string& parent) {
    json j = {{"ts", NowMs()}, {"action", "set_project_parent"}, {"project", project}, {"parent", parent}};
    Record(j.dump());
}

void ReplayRecorder::RecordLinkArticleToProject(const std::string& article_link, const std::string& project) {
    json j = {{"ts", NowMs()}, {"action", "link_article_to_project"}, {"article_link", article_link}, {"project", project}};
    Record(j.dump());
}

void ReplayRecorder::RecordUnlinkArticleFromProject(const std::string& article_link, const std::string& project) {
    json j = {{"ts", NowMs()}, {"action", "unlink_article_from_project"}, {"article_link", article_link}, {"project", project}};
    Record(j.dump());
}

void ReplayRecorder::RecordSetDateRange(const std::string& start, const std::string& end) {
    json j = {{"ts", NowMs()}, {"action", "set_date_range"}, {"start", start}, {"end", end}};
    Record(j.dump());
}

void ReplayRecorder::RecordSetSearchQuery(const std::string& query, bool title, bool authors, bool abstract_field) {
    json j = {{"ts", NowMs()}, {"action", "set_search_query"}, {"query", query},
              {"title", title}, {"authors", authors}, {"abstract_field", abstract_field}};
    Record(j.dump());
}

void ReplayRecorder::RecordSetProjectNote(const std::string& project, const std::string& article_link, const std::string& note) {
    json j = {{"ts", NowMs()}, {"action", "set_project_note"}, {"project", project},
              {"article_link", article_link}, {"note", note}};
    Record(j.dump());
}

void ReplayRecorder::RecordRateArticle(const std::string& article_link, int rating) {
    json j = {{"ts", NowMs()}, {"action", "rate_article"}, {"article_link", article_link}, {"rating", rating}};
    Record(j.dump());
}

void ReplayRecorder::RecordForceRetrain() {
    json j = {{"ts", NowMs()}, {"action", "force_retrain"}};
    Record(j.dump());
}

void ReplayRecorder::RecordExportProjectMarkdown(const std::string& project, const std::string& path) {
    json j = {{"ts", NowMs()}, {"action", "export_project_markdown"}, {"project", project}, {"path", path}};
    Record(j.dump());
}

void ReplayRecorder::RecordExportProjectText(const std::string& project, const std::string& path) {
    json j = {{"ts", NowMs()}, {"action", "export_project_text"}, {"project", project}, {"path", path}};
    Record(j.dump());
}

void ReplayRecorder::RecordExportProjectJSON(const std::string& project, const std::string& path) {
    json j = {{"ts", NowMs()}, {"action", "export_project_json"}, {"project", project}, {"path", path}};
    Record(j.dump());
}

void ReplayRecorder::RecordImportProjectJSON(const std::string& path) {
    json j = {{"ts", NowMs()}, {"action", "import_project_json"}, {"path", path}};
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

    const std::string action = j["action"].get<std::string>();

    if (action == "set_filter_index") {
        core.SetFilterIndex(j.value("index", 0));
    } else if (action == "set_article_index") {
        core.SetArticleIndex(j.value("index", 0));
    } else if (action == "toggle_bookmark") {
        core.ToggleBookmark(j.value("article_link", ""));
    } else if (action == "download_article") {
        core.DownloadArticle(j.value("article_id", ""));
    } else if (action == "add_project") {
        core.AddProject(j.value("name", ""));
    } else if (action == "remove_project") {
        core.RemoveProject(j.value("name", ""));
    } else if (action == "set_project_parent") {
        core.SetProjectParent(j.value("project", ""), j.value("parent", ""));
    } else if (action == "link_article_to_project") {
        core.LinkArticleToProject(j.value("article_link", ""), j.value("project", ""));
    } else if (action == "unlink_article_from_project") {
        core.UnlinkArticleFromProject(j.value("article_link", ""), j.value("project", ""));
    } else if (action == "set_date_range") {
        core.SetDateRange(j.value("start", ""), j.value("end", ""));
    } else if (action == "set_search_query") {
        core.SetSearchQuery(
            j.value("query", ""),
            j.value("title", true),
            j.value("authors", true),
            j.value("abstract_field", true));
    } else if (action == "set_project_note") {
        core.SetProjectNote(j.value("project", ""), j.value("article_link", ""), j.value("note", ""));
    } else if (action == "rate_article") {
        core.RateArticle(j.value("article_link", ""), j.value("rating", 0));
    } else if (action == "force_retrain") {
        core.ForceRetrain();
    } else if (action == "export_project_markdown") {
        core.ExportProjectMarkdown(j.value("project", ""), j.value("path", ""));
    } else if (action == "export_project_text") {
        core.ExportProjectText(j.value("project", ""), j.value("path", ""));
    } else if (action == "export_project_json") {
        core.ExportProjectJSON(j.value("project", ""), j.value("path", ""));
    } else if (action == "import_project_json") {
        core.ImportProjectJSON(j.value("path", ""));
    } else {
        // Unknown action — skip
        return false; // caller treats this as skipped, not error
    }

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
