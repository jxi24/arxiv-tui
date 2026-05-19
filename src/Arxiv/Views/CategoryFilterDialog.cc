#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"
#include "spdlog/spdlog.h"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupCategoryFilterDialog() {
    category_dialog = Renderer([&] {
        if (dialog_depth != Dialog::CategoryFilter) return emptyElement();

        const auto& topics = core.GetTopics();
        Elements rows;
        rows.push_back(text("Filter by arXiv Category") | bold | color(TextColors::primary()));
        rows.push_back(separator() | color(TextColors::border()));
        if (topics.empty()) {
            rows.push_back(text("  (no topics configured)") | color(TextColors::subtext()));
        } else {
            for (int i = 0; i < static_cast<int>(topics.size()); ++i) {
                const auto& t = topics[static_cast<size_t>(i)];
                bool active   = core.IsCategoryActive(t);
                bool selected = (i == category_selected_index);
                std::string mark = active ? "[x] " : "[ ] ";
                auto row = text("  " + mark + t);
                if (selected)
                    rows.push_back(row | bold | color(TextColors::base()) | bgcolor(TextColors::primary()));
                else
                    rows.push_back(row | color(active ? TextColors::text() : TextColors::subtext()));
            }
        }
        rows.push_back(separator() | color(TextColors::border()));
        {
            auto all = core.GetCurrentArticles();
            int uncategorized = 0;
            for (const auto& a : all) {
                if (a.category.empty()) ++uncategorized;
            }
            std::string count_line = "  Showing " + std::to_string(all.size()) + " article(s)";
            if (uncategorized > 0)
                count_line += "  (" + std::to_string(uncategorized) + " uncategorized, always shown)";
            rows.push_back(text(count_line) | color(TextColors::subtext()));
        }
        rows.push_back(separator() | color(TextColors::border()));
        rows.push_back(hbox({
            text("j/k") | bold | color(TextColors::primary()),
            text(": move  ") | color(TextColors::subtext()),
            text("Space/Enter") | bold | color(TextColors::primary()),
            text(": toggle  ") | color(TextColors::subtext()),
            text("a") | bold | color(TextColors::primary()),
            text(": all  ") | color(TextColors::subtext()),
            text("n") | bold | color(TextColors::primary()),
            text(": none  ") | color(TextColors::subtext()),
            text("Esc") | bold | color(TextColors::primary()),
            text(": close") | color(TextColors::subtext()),
        }));
        return vbox(std::move(rows))
            | borderStyled(ROUNDED, TextColors::border())
            | bgcolor(TextColors::surface())
            | clear_under
            | center;
    });
}

bool ArxivApp::HandleCategoryFilterEvent(ftxui::Event event) {
    const auto& topics = core.GetTopics();
    int n = static_cast<int>(topics.size());
    if (event == Event::Escape) {
        dialog_depth = Dialog::None;
        return true;
    }
    if (n == 0) return true;
    if (key_bindings.matches(event, KeyBindings::Action::Next)) {
        category_selected_index = std::min(category_selected_index + 1, n - 1);
        return true;
    }
    if (key_bindings.matches(event, KeyBindings::Action::Previous)) {
        category_selected_index = std::max(category_selected_index - 1, 0);
        return true;
    }
    if (event == Event::Return || event == Event::Character(' ')) {
        const std::string& cat = topics[static_cast<size_t>(category_selected_index)];
        if (m_recorder) m_recorder->RecordToggleCategory(cat);
        spdlog::debug("toggle_category cat={}", cat);
        core.ToggleCategory(cat);
        return true;
    }
    if (event == Event::Character('a')) {
        std::vector<std::string> all(topics.begin(), topics.end());
        if (m_recorder) m_recorder->RecordSetActiveCategories(all);
        spdlog::debug("set_active_categories all={}", all.size());
        core.SetActiveCategories(std::set<std::string>(topics.begin(), topics.end()));
        return true;
    }
    if (event == Event::Character('n')) {
        if (m_recorder) m_recorder->RecordSetActiveCategories({});
        spdlog::debug("set_active_categories none");
        core.SetActiveCategories({});
        return true;
    }
    return true;
}
