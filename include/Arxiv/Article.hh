// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARXIV_ARTICLE
#define ARXIV_ARTICLE

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>

namespace Arxiv {

using time_point = std::chrono::system_clock::time_point;

struct Article {
    std::string title;
    std::string link;
    std::string abstract;
    std::string authors;
    time_point date;
    std::string category;
    bool bookmarked{false};
    // True if this entry is a "replacement" (later version of an existing
    // submission) per arXiv's announce_type / dc:type. The New Articles view
    // hides these so users only see truly fresh submissions.
    bool is_replacement{false};
    // True when the article has been opened in the detail pane or downloaded.
    bool read{false};

    Article() = default;

    Article(std::string _title,
            std::string _link,
            std::string _abstract,
            std::string _authors,
            time_point _date,
            std::string _category,
            bool _bookmarked = false)
        : title(std::move(_title))
        , link(std::move(_link))
        , abstract(std::move(_abstract))
        , authors(std::move(_authors))
        , date(_date)
        , category(std::move(_category))
        , bookmarked(_bookmarked) {}

    bool operator<(const Article& other) const { return date < other.date; }

    bool operator==(const Article& other) const {
        return title == other.title && link == other.link && abstract == other.abstract &&
               authors == other.authors && date == other.date && category == other.category &&
               bookmarked == other.bookmarked;
    }

    /// arXiv identifier: everything after "/abs/" or "/pdf/" (minus a ".pdf"
    /// extension). Old-style identifiers keep their archive prefix
    /// ("hep-ph/0101001"), which a last-slash split lost.
    std::string id() const {
        for (std::string_view kind : {"/abs/", "/pdf/"}) {
            auto pos = link.find(kind);
            if (pos == std::string::npos)
                continue;
            std::string result = link.substr(pos + kind.size());
            constexpr std::string_view ext = ".pdf";
            if (result.size() > ext.size() &&
                result.compare(result.size() - ext.size(), ext.size(), ext) == 0)
                result.erase(result.size() - ext.size());
            return result;
        }
        auto last_slash = link.find_last_of('/');
        return last_slash == std::string::npos ? std::string() : link.substr(last_slash + 1);
    }

    /// id() made safe for a file name ("hep-ph/0101001" -> "hep-ph_0101001").
    std::string file_stem() const {
        std::string stem = id();
        std::replace(stem.begin(), stem.end(), '/', '_');
        return stem;
    }
};

} // namespace Arxiv

#endif
