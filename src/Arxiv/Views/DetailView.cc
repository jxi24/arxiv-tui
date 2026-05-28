// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupDetailView() {
    detail_view = Renderer([&] {
        auto articles = core.GetCurrentArticles();
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

        Elements detail_elements = {
            text("Title: " + article.title) | bold | color(TextColors::primary()),
            paragraph("Authors: " + authors_display) | color(TextColors::text()),
            text("Link: " + article.link) | color(TextColors::secondary()),
            separator() | color(TextColors::border()),
            paragraph("Abstract: \n" + article.abstract) | color(TextColors::text()),
        };

        if (core.GetFilterView() == AppCore::FilterView::Project) {
            std::string proj = core.GetProjectNameForFilter(core.GetFilterIndex());
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
