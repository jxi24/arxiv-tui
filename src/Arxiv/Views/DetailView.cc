// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupDetailView() {
    detail_view = Renderer([&] {
        const auto& articles = core.GetCurrentArticles();
        if (articles.empty() || core.GetArticleIndex() >= static_cast<int>(articles.size())) {
            return window(text("Detail View") | color(TextColors::primary()),
                          text("No details available.") | center | color(TextColors::subtext()));
        }

        const auto& article = articles[static_cast<size_t>(core.GetArticleIndex())];

        std::string authors_display = article.authors;
        size_t comma_count = 0;
        size_t last_comma_pos = 0;
        for (size_t i = 0; i < authors_display.length(); i++) {
            if (authors_display[i] == ',') {
                comma_count++;
                last_comma_pos = i;
                if (comma_count >= 14) {
                    authors_display = authors_display.substr(0, last_comma_pos) + ", et. al.";
                    break;
                }
            }
        }

        std::string meta = "arXiv:" + article.id();
        if (!article.category.empty())
            meta += "   Category: " + article.category;
        const int rating = core.GetArticleRating(article.link);
        std::string stars = "not rated";
        if (rating > 0) {
            stars.clear();
            for (int i = 1; i <= 5; ++i)
                stars += i <= rating ? "★" : "☆";
        }
        meta += "   Rating: " + stars;

        Elements detail_elements = {
            text("Title: " + article.title) | bold | color(TextColors::primary()),
            paragraph("Authors: " + authors_display) | color(TextColors::text()),
            text("Link: " + article.link) | color(TextColors::secondary()),
            paragraph(meta) | color(TextColors::subtext()),
        };
        // Tags and projects only when the paper has some.
        auto joined = [](const std::vector<std::string>& names, const std::string& prefix) {
            std::string out;
            for (const auto& name : names)
                out += (out.empty() ? "" : ", ") + prefix + name;
            return out;
        };
        if (const auto tags = core.GetTagsForArticle(article.link); !tags.empty())
            detail_elements.push_back(paragraph("Tags: " + joined(tags, "#")) |
                                      color(TextColors::subtext()));
        if (const auto projects = core.GetProjectsForArticle(article.link); !projects.empty())
            detail_elements.push_back(paragraph("Projects: " + joined(projects, "")) |
                                      color(TextColors::subtext()));
        detail_elements.push_back(separator() | color(TextColors::border()));
        detail_elements.push_back(paragraph("Abstract: \n" + article.abstract) |
                                  color(TextColors::text()));

        if (const std::string proj = core.CurrentProject(); !proj.empty()) {
            std::string note = core.GetProjectNote(proj, article.link);
            if (!note.empty()) {
                detail_elements.push_back(separator() | color(TextColors::border()));
                detail_elements.push_back(paragraph("Note: " + note) |
                                          color(TextColors::secondary()));
            }
        }

        return vbox(detail_elements) | borderStyled(ROUNDED, TextColors::border());
    });
}
