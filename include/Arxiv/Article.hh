#ifndef ARXIV_ARTICLE
#define ARXIV_ARTICLE

#include "spdlog/spdlog.h"
#include <chrono>
#include <string>

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

    Article() = default;

    Article(std::string _title, std::string _link, std::string _abstract, 
            std::string _authors, time_point _date, std::string _category, bool _bookmarked = false)
        : title(std::move(_title))
        , link(std::move(_link))
        , abstract(std::move(_abstract))
        , authors(std::move(_authors))
        , date(_date)
        , category(std::move(_category))
        , bookmarked(_bookmarked)
    {}

    bool operator<(const Article &other) const {
        return date < other.date;
    }

    bool operator==(const Article &other) const {
        return title == other.title &&
               link == other.link &&
               abstract == other.abstract &&
               authors == other.authors &&
               date == other.date &&
               category == other.category &&
               bookmarked == other.bookmarked;
    }

    std::string id() const {
        size_t last_slash_pos = link.find_last_of('/');
        if(last_slash_pos == std::string::npos) {
            spdlog::error("[Article]: Invalid link format {}", link);
            return "";
        }
        return link.substr(last_slash_pos+1);
    }
};

}

#endif 
