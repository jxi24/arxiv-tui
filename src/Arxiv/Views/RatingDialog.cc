#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupRatingDialog() {
    rating_dialog = Renderer([&] {
        if (dialog_depth != Dialog::Rating) return emptyElement();

        auto articles = core.GetCurrentArticles();
        std::string article_title = articles.empty() ? "" :
            articles[static_cast<size_t>(core.GetArticleIndex())].title;
        int current_rating = articles.empty() ? 0 :
            core.GetArticleRating(articles[static_cast<size_t>(core.GetArticleIndex())].link);

        std::vector<Element> stars;
        for (int i = 1; i <= 5; ++i) {
            std::string label = std::to_string(i) + " star" + (i > 1 ? "s" : "");
            Element item = text("  " + label) | color(TextColors::text());
            if (i == pending_rating) {
                item = text("> " + label) | bold | color(TextColors::primary());
            }
            if (i == current_rating) {
                item = item | color(TextColors::secondary());
            }
            stars.push_back(item);
        }

        return vbox({
            text("Rate Article") | bold | color(TextColors::primary()),
            separator() | color(TextColors::border()),
            paragraph(article_title) | color(TextColors::subtext()),
            separator() | color(TextColors::border()),
            text(current_rating > 0
                 ? "Current rating: " + std::to_string(current_rating) + "/5"
                 : "Not yet rated") | color(TextColors::subtext()),
            separator() | color(TextColors::border()),
            vbox(stars),
            separator() | color(TextColors::border()),
            text("j/k to select, Enter to save, Esc to cancel") | color(TextColors::subtext()),
        }) | borderStyled(ROUNDED, TextColors::border()) | bgcolor(TextColors::surface()) | clear_under | center;
    });
}

bool ArxivApp::HandleRatingEvent(ftxui::Event event) {
    if (event == Event::Return) {
        if (pending_rating >= 1 && pending_rating <= 5) {
            auto articles = core.GetCurrentArticles();
            if (!articles.empty()) {
                const std::string& link = articles[static_cast<size_t>(core.GetArticleIndex())].link;
                core.RateArticle(link, pending_rating);
                if (m_recorder) m_recorder->RecordRateArticle(link, pending_rating);
            }
        }
        dialog_depth = Dialog::None;
        pending_rating = 0;
        return true;
    }
    if (key_bindings.matches(event, KeyBindings::Action::Next)) {
        pending_rating = std::min(pending_rating + 1, 5);
        return true;
    }
    if (key_bindings.matches(event, KeyBindings::Action::Previous)) {
        pending_rating = std::max(pending_rating - 1, 1);
        return true;
    }
    if (event.is_character() && event.character().size() == 1) {
        char c = event.character()[0];
        if (c >= '1' && c <= '5') {
            pending_rating = c - '0';
        }
        return true;
    }
    if (event == Event::Escape) {
        dialog_depth = Dialog::None;
        pending_rating = 0;
        return true;
    }
    return true;
}
