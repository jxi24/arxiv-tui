// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Headless TUI tests for ArxivApp using FTXUI's component rendering API.
// Components can receive events and render to a fixed-size screen without
// a real terminal, enabling verification of dialog state and output.

#include "Arxiv/App.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/KeyBindings.hh"
#include "Arxiv/Views/Colors.hh"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unistd.h>

#include "fixtures/TimeZone.hh"
#include "fixtures/test_data.hh"
#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"

using namespace ftxui;
using namespace Arxiv;
using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock = arxiv_tui::test::FetcherMock;

// ---------------------------------------------------------------------------
// Helper: build an ArxivApp with mock dependencies.
// ---------------------------------------------------------------------------

static Config make_test_config() {
    Config cfg;
    cfg.set_topics({"hep-ph"});
    cfg.set_download_dir("/tmp");
    return cfg;
}

static std::unique_ptr<ArxivApp> make_app(Config cfg = make_test_config()) {
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    return std::make_unique<ArxivApp>(
        std::move(cfg), std::move(db), std::move(fetcher), std::move(kb));
}

// Helper that exposes the mock pointer for expectation setup.
struct AppWithMocks {
    DatabaseManagerMock* db;
    std::unique_ptr<ArxivApp> app;
};

static AppWithMocks make_app_with_articles(Config cfg = make_test_config()) {
    auto db = std::make_unique<DatabaseManagerMock>();
    auto* db_ptr = db.get();
    auto fetcher = std::make_unique<FetcherMock>();
    db_ptr->setArticles(arxiv_tui::test::fixtures::sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({});
    db_ptr->setUnreadArticles(arxiv_tui::test::fixtures::sample_articles);
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    auto app = std::make_unique<ArxivApp>(
        std::move(cfg), std::move(db), std::move(fetcher), std::move(kb));
    return {db_ptr, std::move(app)};
}

// Render the event_handler component to a string at a fixed 120×40 size.
static std::string render(ArxivApp& app) {
    auto screen = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(40));
    ftxui::Render(screen, app.GetEventHandler()->Render());
    return screen.ToString();
}

// ---------------------------------------------------------------------------
// KeyBindings: Settings action has default key S
// ---------------------------------------------------------------------------

TEST_CASE("KeyBindings: Settings action defaults to S", "[tui][keybindings]") {
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    REQUIRE(kb.get_key(KeyBindings::Action::Settings) == "S");
}

// ---------------------------------------------------------------------------
// Settings dialog: pressing S opens it
// ---------------------------------------------------------------------------

TEST_CASE("ArxivApp: pressing S opens the settings dialog", "[tui][settings]") {
    auto app = make_app();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("S"));
    std::string output = render(*app);

    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Settings"));
}

// ---------------------------------------------------------------------------
// Settings dialog: pressing Esc closes it
// ---------------------------------------------------------------------------

TEST_CASE("ArxivApp: pressing Esc closes the settings dialog", "[tui][settings]") {
    auto app = make_app();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("S")); // open
    handler->OnEvent(Event::Escape);         // close

    std::string output = render(*app);
    // After closing, "Settings" header should not appear in the modal overlay.
    // The help overlay and filter pane may still show "Settings" as a key hint,
    // so check specifically for the dialog title text.
    REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("── Settings ──"));
}

// ---------------------------------------------------------------------------
// Settings dialog: General section shows download_dir and auto_refresh fields
// ---------------------------------------------------------------------------

TEST_CASE("ArxivApp: settings dialog General section shows download_dir", "[tui][settings]") {
    Config cfg = make_test_config();
    cfg.set_download_dir("/custom/path");

    auto app = make_app(cfg);
    app->GetEventHandler()->OnEvent(Event::Character("S"));

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Download dir"));
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("/custom/path"));
}

TEST_CASE("ArxivApp: settings dialog General section shows auto-refresh", "[tui][settings]") {
    Config cfg = make_test_config();
    cfg.set_auto_refresh_minutes(15);

    auto app = make_app(cfg);
    app->GetEventHandler()->OnEvent(Event::Character("S"));

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Auto-refresh"));
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("15"));
}

// ---------------------------------------------------------------------------
// Settings dialog: Topics section shows configured topics
// ---------------------------------------------------------------------------

TEST_CASE("ArxivApp: settings dialog Topics section shows configured topics", "[tui][settings]") {
    Config cfg = make_test_config();
    cfg.set_topics({"hep-ph", "cs.LG"});

    auto app = make_app(cfg);
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("S")); // open settings
    // Navigate to Topics section (Tab moves to next section)
    handler->OnEvent(Event::Tab);

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("hep-ph"));
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("cs.LG"));
}

// ---------------------------------------------------------------------------
// Read marking: navigating while detail panel is open marks articles as read
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Rating dialog: n always rates focused article, W rates the selection
// ---------------------------------------------------------------------------

TEST_CASE("ArxivApp: n opens Rate Article dialog even when articles are selected",
          "[tui][rating]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("l")); // move to article pane
    handler->OnEvent(Event::Character(" ")); // select the focused article

    handler->OnEvent(Event::Character("n")); // rate article — must ignore selection

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Rate Article"));
    REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("Rate Selection"));
}

TEST_CASE("ArxivApp: W opens Rate Selection dialog when articles are selected", "[tui][rating]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("l")); // move to article pane
    handler->OnEvent(Event::Character(" ")); // select the focused article

    handler->OnEvent(Event::Character("W")); // rate selection

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Rate Selection"));
}

TEST_CASE("ArxivApp: W shows error when no articles are selected", "[tui][rating]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("l")); // move to article pane
    // No Space pressed — nothing selected

    handler->OnEvent(Event::Character("W")); // rate selection with empty selection

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("No articles selected"));
}

// ---------------------------------------------------------------------------
// Search dialog: two-mode interaction (text entry / search-area selection)
// ---------------------------------------------------------------------------

TEST_CASE("ArxivApp: search dialog starts in text entry and typing builds the query",
          "[tui][search]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("/")); // open search
    handler->OnEvent(Event::Character("a"));
    handler->OnEvent(Event::Character("b"));

    std::string output = render(*app);
    // Query line is the focused line in text-entry mode (marked with ">").
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("> Query: ab"));
}

TEST_CASE("ArxivApp: in text-entry mode j and k are typed into the query, not navigation",
          "[tui][search]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("/")); // open search
    handler->OnEvent(Event::Character("j"));
    handler->OnEvent(Event::Character("k"));

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Query: jk"));
}

TEST_CASE("ArxivApp: Tab switches search dialog into area selection and highlights active area",
          "[tui][search]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("/")); // open search (defaults to Title field)
    handler->OnEvent(Event::Tab);            // switch to area selection

    std::string output = render(*app);
    // The cursor (">") lands on the active area, which is Title by default.
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("> [X] Title"));
}

TEST_CASE("ArxivApp: in area selection j moves cursor and Space toggles the active field",
          "[tui][search]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("/")); // open search
    handler->OnEvent(Event::Tab);            // area selection, cursor on Title
    handler->OnEvent(Event::Character("j")); // move cursor to Authors
    handler->OnEvent(Event::Character(" ")); // toggle Authors as the active field

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("> [X] Authors"));
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("[ ] Title"));
}

TEST_CASE("ArxivApp: in area selection k moves the cursor up", "[tui][search]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("/")); // open search
    handler->OnEvent(Event::Tab);            // area selection, cursor on Title
    handler->OnEvent(Event::Character("k")); // wrap up to Abstract

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("> [ ] Abstract"));
}

TEST_CASE("ArxivApp: Tab from area selection returns to text entry", "[tui][search]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("/")); // open search
    handler->OnEvent(Event::Tab);            // to area selection
    handler->OnEvent(Event::Tab);            // back to text entry
    handler->OnEvent(Event::Character("z")); // should type into the query

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("> Query: z"));
}

TEST_CASE("ArxivApp: Esc closes the search dialog", "[tui][search]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    handler->OnEvent(Event::Character("/")); // open search
    handler->OnEvent(Event::Escape);         // close

    std::string output = render(*app);
    REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("Search Articles"));
}

TEST_CASE("ArxivApp: navigating with detail panel open marks article as read",
          "[tui][read][detail]") {
    auto [db_ptr, app] = make_app_with_articles();
    auto handler = app->GetEventHandler();

    // Move focus to the article pane (default key: l), then open the detail panel.
    handler->OnEvent(Event::Character("l"));
    handler->OnEvent(Event::Character("a"));

    // Navigating to the next article while the panel is open must mark it as read.
    const std::string& second_link = arxiv_tui::test::fixtures::sample_articles[1].link;
    REQUIRE_CALL(*db_ptr, MarkArticleRead(second_link));
    handler->OnEvent(Event::Character("j"));
}

// ---------------------------------------------------------------------------
// Regressions found in code review — each fails until the bug is fixed.
// ---------------------------------------------------------------------------

TEST_CASE("ArxivApp regression: reopening settings shows the values saved last time",
          "[tui][settings][regression]") {
    auto app = make_app(); // download_dir "/tmp"
    auto handler = app->GetEventHandler();
    handler->OnEvent(Event::Character("S"));
    handler->OnEvent(Event::Return);         // edit "Download dir"
    handler->OnEvent(Event::Character("X")); // "/tmpX"
    handler->OnEvent(Event::Return);         // commit
    handler->OnEvent(Event::Escape);         // save and close

    handler->OnEvent(Event::Character("S")); // reopen

    REQUIRE_THAT(render(*app), Catch::Matchers::ContainsSubstring("/tmpX"));
}

TEST_CASE("ArxivApp regression: saved settings take effect without a restart",
          "[tui][settings][regression]") {
    auto app = make_app();
    auto handler = app->GetEventHandler();
    handler->OnEvent(Event::Character("S"));
    handler->OnEvent(Event::Return);
    handler->OnEvent(Event::Character("X"));
    handler->OnEvent(Event::Return);
    handler->OnEvent(Event::Escape);

    REQUIRE(app->GetCore().GetConfig().get_download_dir() == "/tmpX");
}

TEST_CASE("ArxivApp regression: navigation keys can be typed into a settings field",
          "[tui][settings][regression]") {
    auto app = make_app();
    auto handler = app->GetEventHandler();
    handler->OnEvent(Event::Character("S"));
    handler->OnEvent(Event::Return); // edit "Download dir" = "/tmp"

    handler->OnEvent(Event::Character("j"));
    handler->OnEvent(Event::Character("k"));

    REQUIRE_THAT(render(*app), Catch::Matchers::ContainsSubstring("/tmpjk"));
}

// ---------------------------------------------------------------------------
// Help overlay: grouped by task, keys first, nothing truncated, conflicts shown
// ---------------------------------------------------------------------------

TEST_CASE("Help overlay groups bindings by task", "[tui][help]") {
    auto app = make_app();
    app->GetEventHandler()->OnEvent(Event::Character("?"));
    const std::string out = render(*app);

    SECTION("navigation") { REQUIRE_THAT(out, Catch::Matchers::ContainsSubstring("Navigation")); }
    SECTION("export") { REQUIRE_THAT(out, Catch::Matchers::ContainsSubstring("Export & share")); }
}

TEST_CASE("Help overlay shows every action name and key in full", "[tui][help]") {
    auto app = make_app();
    app->GetEventHandler()->OnEvent(Event::Character("?"));
    const std::string out = render(*app);

    SECTION("long action name") {
        REQUIRE_THAT(out, Catch::Matchers::ContainsSubstring("Export Selected Digest"));
    }
    SECTION("space key spelled out, not cut off") {
        REQUIRE_THAT(out, !Catch::Matchers::ContainsSubstring("<space"));
        REQUIRE_THAT(out, Catch::Matchers::ContainsSubstring("Toggle Selection"));
    }
}

TEST_CASE("Help overlay warns about keys bound to two actions", "[tui][help]") {
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    KeyBindings kb{std::vector<Config::KeyMapping>{{"next", "b"}}}; // b is also Bookmark
    auto app = std::make_unique<ArxivApp>(
        make_test_config(), std::move(db), std::move(fetcher), std::move(kb));
    app->GetEventHandler()->OnEvent(Event::Character("?"));

    REQUIRE_THAT(render(*app), Catch::Matchers::ContainsSubstring("bound twice"));
}

// ---------------------------------------------------------------------------
// Article list layout (TUI audit)
// ---------------------------------------------------------------------------

TEST_CASE("Article list keeps the title readable when many columns are configured",
          "[tui][layout]") {
    // With title, date, authors and category on an 80-column terminal the
    // title used to get 8 characters while authors took 24. Optional columns
    // are dropped from the right until the title has room.
    Config cfg = make_test_config();
    cfg.set_article_columns({"title", "date", "authors", "category"});
    auto [db, app] = make_app_with_articles(cfg);
    app->GetCore().SetFilterIndex(AppCore::FilterView::All);

    REQUIRE_THAT(render(*app), Catch::Matchers::ContainsSubstring("Sample Article Title"));
}

TEST_CASE("An empty Bookmarks view says how to add one", "[tui][layout]") {
    auto app = make_app();
    app->GetCore().SetFilterIndex(AppCore::FilterView::Bookmarks);

    REQUIRE_THAT(render(*app), Catch::Matchers::ContainsSubstring("press b"));
}

TEST_CASE("The footer drops the topic list rather than overlap on a narrow terminal",
          "[tui][layout]") {
    Config cfg = make_test_config();
    cfg.set_topics({"hep-ph", "hep-ex", "hep-lat", "hep-th", "astro-ph.CO"});
    auto [db, app] = make_app_with_articles(cfg);
    app->GetCore().SetFilterIndex(AppCore::FilterView::All);

    auto screen = Screen::Create(Dimension::Fixed(80), Dimension::Fixed(24));
    ftxui::Render(screen, app->GetEventHandler()->Render());
    const std::string out = screen.ToString();
    REQUIRE_THAT(out, !Catch::Matchers::ContainsSubstring("2hep"));
}

TEST_CASE("The configured theme is applied when the app starts", "[tui][theme]") {
    Config cfg = make_test_config();
    cfg.set_theme("high-contrast");
    auto app = make_app(cfg);
    const ftxui::Color hc_base = TextColors::base();
    Arxiv::SetTheme("frappe");
    REQUIRE_FALSE(hc_base == TextColors::base());
}

TEST_CASE("Settings lets the user cycle the theme", "[tui][theme][settings]") {
    auto app = make_app();
    auto handler = app->GetEventHandler();
    handler->OnEvent(Event::Character("S"));
    for (int i = 0; i < 3; ++i) // General: download dir, auto-refresh, scroll margin, theme
        handler->OnEvent(Event::Character("j"));
    REQUIRE_THAT(render(*app), Catch::Matchers::ContainsSubstring("Theme"));

    handler->OnEvent(Event::Return); // next preset
    REQUIRE_THAT(render(*app), Catch::Matchers::ContainsSubstring("latte"));
    Arxiv::SetTheme("frappe");
}

// ---------------------------------------------------------------------------
// Bad config pop-up
// ---------------------------------------------------------------------------

TEST_CASE("A bad config value is reported in a pop-up at startup", "[tui][config]") {
    const auto path = (std::filesystem::temp_directory_path() /
                       ("arxiv_tui_badcfg_" + std::to_string(::getpid()) + ".yml"))
                          .string();
    {
        std::ofstream f(path);
        f << "article_settings:\n  download_dir: /tmp\n  topics: [hep-ph]\n"
          << "retrain_interval: five\n";
    }
    Config cfg(path);
    std::filesystem::remove(path);
    auto app = make_app(cfg);

    const std::string out = render(*app);
    SECTION("names the key") {
        REQUIRE_THAT(out, Catch::Matchers::ContainsSubstring("retrain_interval"));
    }
    SECTION("says the default is used") {
        REQUIRE_THAT(out, Catch::Matchers::ContainsSubstring("default"));
    }
    SECTION("dismissed with Enter") {
        app->GetEventHandler()->OnEvent(Event::Return);
        REQUIRE_THAT(render(*app), !Catch::Matchers::ContainsSubstring("retrain_interval"));
    }
}

TEST_CASE("A valid config shows no pop-up", "[tui][config]") {
    auto app = make_app();
    REQUIRE_THAT(render(*app), !Catch::Matchers::ContainsSubstring("Config problem"));
}

// ---------------------------------------------------------------------------
// Followed-authors dialog (f)
// ---------------------------------------------------------------------------

namespace {

// Real in-memory DB; the newest article (focused first) is by Wei Li and Jane Smith.
std::unique_ptr<ArxivApp> make_follow_app() {
    static int counter = 0;
    auto db = std::make_unique<DatabaseManager>(":memory:");
    const auto now = std::chrono::system_clock::now();
    db->AddArticle(Article(
        "Newest", "https://arxiv.org/abs/2401.00100", "A", "Wei Li, Jane Smith", now, "hep-ph"));
    db->AddArticle(Article("Older",
                           "https://arxiv.org/abs/2401.00101",
                           "B",
                           "Xiao Li",
                           now - std::chrono::hours(48),
                           "hep-ph"));
    Config cfg = make_test_config();
    cfg.set_ranker_file((std::filesystem::temp_directory_path() /
                         ("arxiv_follow_ui_" + std::to_string(counter++) + ".bin"))
                            .string());
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    auto app = std::make_unique<ArxivApp>(
        cfg, std::move(db), std::make_unique<FetcherMock>(), std::move(kb));
    app->GetCore().SetFilterIndex(AppCore::FilterView::All);
    return app;
}

void type(ArxivApp& app, const std::string& s) {
    for (char c : s)
        app.GetEventHandler()->OnEvent(Event::Character(c));
}

} // namespace

TEST_CASE("KeyBindings: Follow Authors defaults to f", "[tui][follow]") {
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    REQUIRE(kb.get_key(KeyBindings::Action::FollowAuthors) == "f");
}

TEST_CASE("f opens the followed-authors dialog with the focused article's authors",
          "[tui][follow]") {
    auto app = make_follow_app();
    app->GetEventHandler()->OnEvent(Event::Character("f"));
    const std::string out = render(*app);

    SECTION("title") { REQUIRE_THAT(out, Catch::Matchers::ContainsSubstring("Followed Authors")); }
    SECTION("first author") { REQUIRE_THAT(out, Catch::Matchers::ContainsSubstring("Wei Li")); }
    SECTION("second author") {
        REQUIRE_THAT(out, Catch::Matchers::ContainsSubstring("Jane Smith"));
    }
}

TEST_CASE("Enter on an article author follows and unfollows them", "[tui][follow]") {
    auto app = make_follow_app();
    auto handler = app->GetEventHandler();
    handler->OnEvent(Event::Character("f"));

    handler->OnEvent(Event::Return); // first row: Wei Li
    SECTION("follows") {
        REQUIRE(app->GetCore().GetFollowedAuthors() == std::vector<std::string>{"Wei Li"});
    }
    SECTION("second Enter unfollows") {
        handler->OnEvent(Event::Return);
        REQUIRE(app->GetCore().GetFollowedAuthors().empty());
    }
}

TEST_CASE("A typed name can be followed", "[tui][follow]") {
    auto app = make_follow_app();
    app->GetEventHandler()->OnEvent(Event::Character("f"));
    app->GetEventHandler()->OnEvent(Event::Character("+"));
    type(*app, "J. K. Rowling"); // j and k are text here, not navigation
    app->GetEventHandler()->OnEvent(Event::Return);

    REQUIRE(app->GetCore().GetFollowedAuthors() == std::vector<std::string>{"J. K. Rowling"});
}

TEST_CASE("Following a name without a first initial asks for confirmation", "[tui][follow]") {
    auto app = make_follow_app();
    auto handler = app->GetEventHandler();
    handler->OnEvent(Event::Character("f"));
    handler->OnEvent(Event::Character("+"));
    type(*app, "Li");
    handler->OnEvent(Event::Return);

    SECTION("warns and waits") {
        REQUIRE_THAT(render(*app), Catch::Matchers::ContainsSubstring("no first initial"));
        REQUIRE(app->GetCore().GetFollowedAuthors().empty());
    }
    SECTION("Enter again follows anyway") {
        handler->OnEvent(Event::Return);
        REQUIRE(app->GetCore().GetFollowedAuthors() == std::vector<std::string>{"Li"});
    }
    SECTION("Esc cancels") {
        handler->OnEvent(Event::Escape);
        REQUIRE(app->GetCore().GetFollowedAuthors().empty());
    }
}

TEST_CASE("A followed name without a first initial is flagged in the list", "[tui][follow]") {
    auto app = make_follow_app();
    app->GetCore().FollowAuthor("Li");
    app->GetEventHandler()->OnEvent(Event::Character("f"));

    REQUIRE_THAT(render(*app), Catch::Matchers::ContainsSubstring("no first initial"));
}

TEST_CASE("d removes a followed author from the list", "[tui][follow]") {
    auto app = make_follow_app();
    app->GetCore().FollowAuthor("X. Li");
    auto handler = app->GetEventHandler();
    handler->OnEvent(Event::Character("f"));
    // Rows: Wei Li, Jane Smith (this article), then X. Li (following).
    handler->OnEvent(Event::Character("j"));
    handler->OnEvent(Event::Character("j"));
    handler->OnEvent(Event::Character("d"));

    REQUIRE(app->GetCore().GetFollowedAuthors().empty());
}

TEST_CASE("TUI shows an article's arXiv day in any time zone", "[tui][regression]") {
    // Stored as 04:00 UTC (midnight US Eastern, the RSS announcement); in Los
    // Angeles that is the evening before.
    arxiv_tui::test::ScopedTimeZone tz("America/Los_Angeles");
    auto article = arxiv_tui::test::fixtures::sample_articles[0];
    article.date = arxiv_tui::test::utc_time(2026, 7, 4, 4);
    auto db = std::make_unique<DatabaseManagerMock>();
    db->setArticles({article});
    db->setBookmarkedArticles({});
    db->setProjects({});
    db->setUnreadArticles({article});
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    ArxivApp app(make_test_config(), std::move(db), std::make_unique<FetcherMock>(), std::move(kb));

    auto screen = render(app);

    REQUIRE_THAT(screen, Catch::Matchers::ContainsSubstring("2026-07-04"));
    REQUIRE_THAT(screen, !Catch::Matchers::ContainsSubstring("2026-07-03"));
}

TEST_CASE("Keyword editor takes every letter as text", "[tui][keywords][regression]") {
    // j/k (next/previous) and d (delete) used to be handled before text, so
    // "dark jets" could not be typed.
    auto file = std::filesystem::temp_directory_path() /
                ("arxiv_kw_editor_" + std::to_string(::getpid()) + ".txt");
    std::filesystem::remove(file);
    Config cfg = make_test_config();
    cfg.set_keywords_file(file.string());
    auto app = make_app_with_articles(cfg).app;
    auto handler = app->GetEventHandler();
    auto type = [&](const std::string& text) {
        for (char c : text)
            handler->OnEvent(Event::Character(std::string(1, c)));
    };

    handler->OnEvent(Event::Character("K"));
    type("dark jets");
    handler->OnEvent(Event::Return);
    type("kaon");
    handler->OnEvent(Event::Return);

    SECTION("both keywords are saved as typed") {
        handler->OnEvent(Event::Escape);
        REQUIRE(app->GetCore().GetKeywords() == std::vector<std::string>{"dark jets", "kaon"});
    }
    SECTION("arrows select and Delete removes") {
        handler->OnEvent(Event::ArrowUp);
        handler->OnEvent(Event::Delete);
        handler->OnEvent(Event::Escape);
        REQUIRE(app->GetCore().GetKeywords() == std::vector<std::string>{"kaon"});
    }
    std::filesystem::remove(file);
}

TEST_CASE("TUI exports go to export_dir, not the working directory", "[tui][export][regression]") {
    auto dir = std::filesystem::temp_directory_path() /
               ("arxiv_tui_exports_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    Config cfg = make_test_config();
    cfg.set_export_dir(dir.string());
    cfg.set_clipboard_backend("arxiv-tui-no-such-clipboard"); // force the file fallback
    auto db = std::make_unique<DatabaseManagerMock>();
    auto* db_ptr = db.get();
    db_ptr->setArticles(arxiv_tui::test::fixtures::sample_articles);
    db_ptr->setBookmarkedArticles({});
    db_ptr->setProjects({});
    db_ptr->setUnreadArticles(arxiv_tui::test::fixtures::sample_articles);
    auto fetcher = std::make_unique<FetcherMock>();
    ALLOW_CALL(*fetcher, FetchBibTeX(ANY(std::string))).RETURN(std::string{});
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    ArxivApp app(std::move(cfg), std::move(db), std::move(fetcher), std::move(kb));
    auto handler = app.GetEventHandler();
    const auto cwd_before = std::distance(std::filesystem::directory_iterator("."),
                                          std::filesystem::directory_iterator());

    SECTION("BibTeX fallback file") {
        ALLOW_CALL(*db_ptr, GetRating(ANY(std::string))).RETURN(0);
        handler->OnEvent(Event::Character("l")); // focus the article list
        handler->OnEvent(Event::Character("c"));
        REQUIRE(std::filesystem::exists(
            dir / (arxiv_tui::test::fixtures::sample_articles[0].id() + ".bib")));
    }
    SECTION("daily digest") {
        handler->OnEvent(Event::Character("X"));
        bool found = false;
        if (std::filesystem::exists(dir))
            for (const auto& e : std::filesystem::directory_iterator(dir))
                found = found || e.path().filename().string().rfind("digest-", 0) == 0;
        REQUIRE(found);
    }
    REQUIRE(std::distance(std::filesystem::directory_iterator("."),
                          std::filesystem::directory_iterator()) == cwd_before);
    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// The filter and article menus used to write AppCore's indices directly, so
// a selection changed by the menu itself (arrow keys, mouse) skipped
// SetFilterIndex / SetArticleIndex.
// ---------------------------------------------------------------------------

namespace {
struct RealDbApp {
    Arxiv::DatabaseManager* db = nullptr;
    std::unique_ptr<ArxivApp> app;
    RealDbApp() {
        auto owned = std::make_unique<Arxiv::DatabaseManager>(":memory:");
        db = owned.get();
        for (const auto& a : arxiv_tui::test::fixtures::sample_articles)
            db->AddArticle(a);
        KeyBindings kb{std::vector<Config::KeyMapping>{}};
        app = std::make_unique<ArxivApp>(
            make_test_config(), std::move(owned), std::make_unique<FetcherMock>(), std::move(kb));
        app->GetCore().SetFilterIndex(Arxiv::AppCore::FilterView::All);
    }
};
} // namespace

TEST_CASE("Moving the filter menu with the arrow keys loads that view", "[tui][regression]") {
    RealDbApp expected;
    expected.app->GetCore().SetFilterIndex(Arxiv::AppCore::FilterView::Bookmarks);
    RealDbApp t;
    REQUIRE(t.app->GetCore().GetCurrentArticles().size() !=
            expected.app->GetCore().GetCurrentArticles().size());

    t.app->GetEventHandler()->OnEvent(Event::ArrowDown); // All -> Bookmarks (filter pane)

    REQUIRE(t.app->GetCore().GetFilterView() == Arxiv::AppCore::FilterView::Bookmarks);
    REQUIRE(t.app->GetCore().GetCurrentArticles().size() ==
            expected.app->GetCore().GetCurrentArticles().size());
}

TEST_CASE("Moving the article menu with the arrow keys marks the article read",
          "[tui][regression]") {
    RealDbApp t;
    auto handler = t.app->GetEventHandler();
    handler->OnEvent(Event::Character("l")); // focus the article list
    handler->OnEvent(Event::Character("a")); // open the detail pane
    const int before = t.app->GetCore().GetArticleIndex();

    handler->OnEvent(Event::ArrowDown);

    const auto& articles = t.app->GetCore().GetCurrentArticles();
    REQUIRE(t.app->GetCore().GetArticleIndex() == before + 1);
    const auto& now = articles[static_cast<size_t>(before + 1)];
    auto unread = t.db->GetUnreadArticles();
    REQUIRE(std::none_of(
        unread.begin(), unread.end(), [&](const Article& a) { return a.link == now.link; }));
}

// ---------------------------------------------------------------------------
// Settings can be left without saving: q discards, Esc in a field discards
// that edit, Esc otherwise saves (as before).
// ---------------------------------------------------------------------------

TEST_CASE("Settings: q closes without saving", "[tui][settings]") {
    auto app = make_app(); // download_dir "/tmp"
    auto handler = app->GetEventHandler();
    handler->OnEvent(Event::Character("S"));
    handler->OnEvent(Event::Return);         // edit "Download dir"
    handler->OnEvent(Event::Character("X")); // "/tmpX"
    handler->OnEvent(Event::Return);         // commit the field

    handler->OnEvent(Event::Character("q")); // discard and close

    SECTION("nothing applied") { REQUIRE(app->GetCore().GetConfig().get_download_dir() == "/tmp"); }
    SECTION("reopening shows the old value") {
        handler->OnEvent(Event::Character("S"));
        REQUIRE_THAT(render(*app), !Catch::Matchers::ContainsSubstring("/tmpX"));
    }
}

TEST_CASE("Settings: Esc while editing a field discards the edit", "[tui][settings]") {
    auto app = make_app();
    auto handler = app->GetEventHandler();
    handler->OnEvent(Event::Character("S"));
    handler->OnEvent(Event::Return);
    handler->OnEvent(Event::Character("X"));
    handler->OnEvent(Event::Escape); // abandon the field edit
    handler->OnEvent(Event::Escape); // save and close

    REQUIRE(app->GetCore().GetConfig().get_download_dir() == "/tmp");
}

TEST_CASE("Settings: q undoes a theme preview", "[tui][settings][theme]") {
    auto app = make_app();
    auto handler = app->GetEventHandler();
    const auto before = Arxiv::CurrentTheme().primary;
    handler->OnEvent(Event::Character("S"));
    for (int i = 0; i < 3; ++i)
        handler->OnEvent(Event::Character("j")); // General -> Theme
    handler->OnEvent(Event::Return);             // previews the next preset
    REQUIRE_FALSE(Arxiv::CurrentTheme().primary == before);

    handler->OnEvent(Event::Character("q"));

    REQUIRE(Arxiv::CurrentTheme().primary == before);
}

// ---------------------------------------------------------------------------
// Tags dialog (#): there was no way to create or apply tags from the TUI.
// ---------------------------------------------------------------------------

TEST_CASE("Tags dialog", "[tui][tags]") {
    RealDbApp t;
    auto handler = t.app->GetEventHandler();
    auto& core = t.app->GetCore();
    const auto focused = core.GetCurrentArticles()[0].link;
    auto type = [&](const std::string& text) {
        for (char c : text)
            handler->OnEvent(Event::Character(std::string(1, c)));
    };
    auto has_tag = [&](const std::string& tag) {
        auto tags = core.GetTagsForArticle(focused);
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    };

    handler->OnEvent(Event::Character("#"));
    REQUIRE_THAT(render(*t.app), Catch::Matchers::ContainsSubstring("Tags"));

    handler->OnEvent(Event::Character("+"));
    type("dark jets"); // letters such as d, j and k are text here
    handler->OnEvent(Event::Return);

    SECTION("a typed tag is created and applied") {
        REQUIRE(has_tag("dark jets"));
        REQUIRE_THAT(render(*t.app), Catch::Matchers::ContainsSubstring("[x] dark jets"));
    }
    SECTION("Enter on a tag toggles it") {
        handler->OnEvent(Event::Return);
        REQUIRE_FALSE(has_tag("dark jets"));
    }
    SECTION("Delete asks, then removes the tag everywhere") {
        handler->OnEvent(Event::Delete);
        REQUIRE(has_tag("dark jets")); // not yet: waiting for confirmation
        handler->OnEvent(Event::Return);
        const auto tags = core.GetTags();
        REQUIRE(std::find(tags.begin(), tags.end(), "dark jets") == tags.end());
    }
    SECTION("Esc closes the dialog") {
        handler->OnEvent(Event::Escape);
        REQUIRE_THAT(render(*t.app), !Catch::Matchers::ContainsSubstring("[x] dark jets"));
    }
}

// ---------------------------------------------------------------------------
// The layout follows the area it is drawn in, not Terminal::Size() (which is
// the 80x24 fallback when there is no terminal, e.g. in these tests).
// ---------------------------------------------------------------------------

namespace {
std::vector<std::string> render_lines(ArxivApp& app, int w, int h) {
    auto screen = Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h));
    for (int pass = 0; pass < 2; ++pass) // the second frame uses measured sizes
        ftxui::Render(screen, app.GetEventHandler()->Render());
    std::vector<std::string> lines;
    std::istringstream in(screen.ToString());
    std::string line;
    while (std::getline(in, line))
        lines.push_back(line);
    return lines;
}

std::unique_ptr<ArxivApp> app_with_many_articles(int n) {
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    auto base = arxiv_tui::test::fixtures::sample_articles[0];
    for (int i = 0; i < n; ++i) {
        auto a = base;
        a.link = "https://arxiv.org/abs/2607." + std::to_string(10000 + i);
        a.title = "Paper number " + std::to_string(i);
        db->AddArticle(a);
    }
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    auto app = std::make_unique<ArxivApp>(
        make_test_config(), std::move(db), std::make_unique<FetcherMock>(), std::move(kb));
    app->GetCore().SetFilterIndex(Arxiv::AppCore::FilterView::All);
    return app;
}
} // namespace

TEST_CASE("Layout fills a wide screen", "[tui][layout][regression]") {
    auto app = app_with_many_articles(5);
    auto lines = render_lines(*app, 160, 30);
    // The article pane's top-right corner sits at the right edge.
    REQUIRE(lines[0].rfind("╮") != std::string::npos);
    const std::string& top = lines[0];
    // Count display columns up to the last corner (box-drawing chars are 3 bytes).
    std::size_t cols = 0;
    for (std::size_t i = 0; i < top.rfind("╮"); ++i)
        if ((static_cast<unsigned char>(top[i]) & 0xC0) != 0x80)
            ++cols;
    REQUIRE(cols >= 150);
}

TEST_CASE("Layout shows as many rows as the screen is tall", "[tui][layout][regression]") {
    auto app = app_with_many_articles(60);
    auto lines = render_lines(*app, 120, 50);
    int rows = 0;
    for (const auto& line : lines)
        if (line.find("Paper number") != std::string::npos)
            ++rows;
    REQUIRE(rows >= 40); // 24-line fallback gave 19
}

TEST_CASE("Layout splits the width evenly with the detail pane open", "[tui][layout]") {
    auto app = app_with_many_articles(5);
    app->GetEventHandler()->OnEvent(Event::Character("a")); // open the detail pane
    std::vector<std::string> lines;
    for (int frame = 0; frame < 5; ++frame) // stable over several frames
        lines = render_lines(*app, 160, 30);
    // Corners of the three panes on the top line, in display columns.
    std::vector<std::size_t> corners;
    std::size_t col = 0;
    const std::string& top = lines[0];
    for (std::size_t i = 0; i < top.size(); ++i) {
        if ((static_cast<unsigned char>(top[i]) & 0xC0) == 0x80)
            continue;
        if (top.compare(i, 3, "╮") == 0)
            corners.push_back(col);
        ++col;
    }
    REQUIRE(corners.size() == 3);
    const auto articles = corners[1] - corners[0];
    const auto detail = corners[2] - corners[1];
    REQUIRE(articles + 2 >= detail);
    REQUIRE(detail + 2 >= articles);
}

// ---------------------------------------------------------------------------
// Status markers live in their own column: as title prefixes ("[*] ", a
// double-width emoji) they pushed titles out of line.
// ---------------------------------------------------------------------------

namespace {
// Display column (not byte offset) of `needle` in `line`.
std::size_t display_col(const std::string& line, const std::string& needle) {
    const auto at = line.find(needle);
    if (at == std::string::npos)
        return std::string::npos;
    std::size_t col = 0;
    for (std::size_t i = 0; i < at; ++i)
        if ((static_cast<unsigned char>(line[i]) & 0xC0) != 0x80)
            ++col;
    return col;
}
} // namespace

TEST_CASE("Article titles line up whatever their markers", "[tui][layout]") {
    RealDbApp t; // sample_articles[1] is bookmarked
    const auto& fx = arxiv_tui::test::fixtures::sample_articles;
    t.app->GetCore().ToggleSelection(fx[0].link);
    auto lines = render_lines(*t.app, 160, 30);
    std::size_t col0 = std::string::npos, col1 = std::string::npos;
    std::string row1;
    for (const auto& line : lines) {
        if (line.find(fx[0].title.substr(0, 20)) != std::string::npos)
            col0 = display_col(line, fx[0].title.substr(0, 20));
        if (line.find(fx[1].title.substr(0, 20)) != std::string::npos) {
            col1 = display_col(line, fx[1].title.substr(0, 20));
            row1 = line;
        }
    }
    REQUIRE(col0 != std::string::npos);
    REQUIRE(col0 == col1);
    REQUIRE_THAT(row1, !Catch::Matchers::ContainsSubstring("⭐"));
}

// ---------------------------------------------------------------------------
// Success messages are toasts: shown, but they do not take the next key.
// ---------------------------------------------------------------------------

TEST_CASE("Success messages do not block the next key", "[tui][toast]") {
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    for (const auto& a : arxiv_tui::test::fixtures::sample_articles)
        db->AddArticle(a);
    auto fetcher = std::make_unique<FetcherMock>();
    ALLOW_CALL(*fetcher, FetchBibTeX(ANY(std::string))).RETURN(std::string{});
    Config cfg = make_test_config();
    cfg.set_clipboard_backend("true"); // a "clipboard" that always succeeds
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    ArxivApp app(std::move(cfg), std::move(db), std::move(fetcher), std::move(kb));
    app.GetCore().SetFilterIndex(Arxiv::AppCore::FilterView::All);
    auto handler = app.GetEventHandler();
    handler->OnEvent(Event::Character("l")); // article list

    handler->OnEvent(Event::Character("c")); // copy BibTeX

    SECTION("the message is shown") {
        REQUIRE_THAT(render(app), Catch::Matchers::ContainsSubstring("BibTeX copied"));
    }
    SECTION("the next key still works") {
        handler->OnEvent(Event::Character("j"));
        REQUIRE(app.GetCore().GetArticleIndex() == 1);
    }
}

// ---------------------------------------------------------------------------
// The footer suggests the keys that make sense where the focus is.
// ---------------------------------------------------------------------------

namespace {
// The footer line without colour codes.
std::string footer_of(ArxivApp& app) {
    auto lines = render_lines(app, 160, 30);
    std::string plain;
    const std::string& line = lines.back();
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\x1b') {
            while (i < line.size() && line[i] != 'm')
                ++i;
            continue;
        }
        plain += line[i];
    }
    return plain;
}
} // namespace

TEST_CASE("Footer hints follow the focus", "[tui][footer]") {
    RealDbApp t;
    auto handler = t.app->GetEventHandler();

    SECTION("filter pane: views and projects") {
        const auto footer = footer_of(*t.app);
        REQUIRE_THAT(footer, Catch::Matchers::ContainsSubstring("new project"));
        REQUIRE_THAT(footer, !Catch::Matchers::ContainsSubstring("bookmark"));
    }
    SECTION("article list: article actions") {
        handler->OnEvent(Event::Character("l"));
        const auto footer = footer_of(*t.app);
        REQUIRE_THAT(footer, Catch::Matchers::ContainsSubstring("bookmark"));
        REQUIRE_THAT(footer, Catch::Matchers::ContainsSubstring("tags"));
    }
    SECTION("with a selection: selection actions") {
        handler->OnEvent(Event::Character("l"));
        handler->OnEvent(Event::Character(" "));
        REQUIRE_THAT(footer_of(*t.app), Catch::Matchers::ContainsSubstring("rate selection"));
    }
    SECTION("rebound keys are shown as bound") {
        Config cfg = make_test_config();
        cfg.set_key_mappings({{"bookmark", "B"}});
        auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
        for (const auto& a : arxiv_tui::test::fixtures::sample_articles)
            db->AddArticle(a);
        ArxivApp app(cfg,
                     std::move(db),
                     std::make_unique<FetcherMock>(),
                     KeyBindings{cfg.get_key_mappings()});
        app.GetCore().SetFilterIndex(Arxiv::AppCore::FilterView::All);
        app.GetEventHandler()->OnEvent(Event::Character("l"));
        REQUIRE_THAT(footer_of(app), Catch::Matchers::ContainsSubstring("B bookmark"));
    }
}

// ---------------------------------------------------------------------------
// The filter pane says how many papers each view holds and labels the tag and
// project sections.
// ---------------------------------------------------------------------------

namespace {
std::vector<std::string> plain_lines(ArxivApp& app) {
    std::vector<std::string> out;
    for (const auto& line : render_lines(app, 160, 30)) {
        std::string plain;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '\x1b') {
                while (i < line.size() && line[i] != 'm')
                    ++i;
                continue;
            }
            plain += line[i];
        }
        out.push_back(plain);
    }
    return out;
}

bool any_line_matches(const std::vector<std::string>& lines, const std::string& pattern) {
    const std::regex re(pattern);
    return std::any_of(
        lines.begin(), lines.end(), [&](const auto& l) { return std::regex_search(l, re); });
}
} // namespace

TEST_CASE("Filter pane shows counts and section headers", "[tui][filters]") {
    RealDbApp t; // sample_articles[1] is bookmarked, nothing is read
    const auto& fx = arxiv_tui::test::fixtures::sample_articles;
    auto& core = t.app->GetCore();
    const auto all = std::to_string(fx.size());

    SECTION("Bookmarks has its count") {
        REQUIRE(any_line_matches(plain_lines(*t.app), "Bookmarks +1 "));
    }
    SECTION("Unread has its count") {
        REQUIRE(any_line_matches(plain_lines(*t.app), "Unread +" + all + " "));
    }
    SECTION("counts follow changes") {
        core.ToggleBookmark(fx[0].link);
        REQUIRE(any_line_matches(plain_lines(*t.app), "Bookmarks +2 "));
    }
    SECTION("no section headers without tags or projects") {
        const auto lines = plain_lines(*t.app);
        REQUIRE_FALSE(any_line_matches(lines, "│ *Tags *│"));
        REQUIRE_FALSE(any_line_matches(lines, "│ *Projects *│"));
    }
    SECTION("tags and projects get headers and counts") {
        core.LinkArticleToTag(fx[0].link, "qcd");
        core.AddProject("thesis");
        core.LinkArticleToProject(fx[0].link, "thesis");
        core.LinkArticleToProject(fx[1].link, "thesis");
        const auto lines = plain_lines(*t.app);
        REQUIRE(any_line_matches(lines, "│ *Tags *│"));
        REQUIRE(any_line_matches(lines, "│ *Projects *│"));
        REQUIRE(any_line_matches(lines, "#qcd +1 "));
        REQUIRE(any_line_matches(lines, "thesis +2 "));
    }
    SECTION("headers are skipped by navigation") {
        core.LinkArticleToTag(fx[0].link, "qcd");
        core.SetFilterIndex(Arxiv::AppCore::FilterView::Unread);
        t.app->GetEventHandler()->OnEvent(Event::Character("j"));
        REQUIRE(core.CurrentTag() == "qcd");
    }
}

// ---------------------------------------------------------------------------
// The detail pane shows what is known about the paper, not just its text.
// ---------------------------------------------------------------------------

TEST_CASE("Detail pane shows id, category, rating, tags and projects", "[tui][detail]") {
    RealDbApp t;
    auto& core = t.app->GetCore();
    const auto focused = core.GetCurrentArticles().front();
    t.db->SetRating(focused.link, 4);
    core.LinkArticleToTag(focused.link, "qcd");
    core.AddProject("thesis");
    core.LinkArticleToProject(focused.link, "thesis");
    core.SetFilterIndex(Arxiv::AppCore::FilterView::All);
    t.app->GetEventHandler()->OnEvent(Event::Character("a")); // open the detail pane
    const auto lines = plain_lines(*t.app);

    SECTION("arXiv id") { REQUIRE(any_line_matches(lines, "arXiv:" + focused.id())); }
    SECTION("category") { REQUIRE(any_line_matches(lines, "Category: " + focused.category)); }
    SECTION("rating") { REQUIRE(any_line_matches(lines, "Rating: ★★★★☆")); }
    SECTION("tags") { REQUIRE(any_line_matches(lines, "Tags: #qcd")); }
    SECTION("projects") { REQUIRE(any_line_matches(lines, "Projects: thesis")); }
}

TEST_CASE("Detail pane leaves out what a paper does not have", "[tui][detail]") {
    RealDbApp t;
    t.app->GetEventHandler()->OnEvent(Event::Character("a"));
    const auto lines = plain_lines(*t.app);
    REQUIRE_FALSE(any_line_matches(lines, "Tags:"));
    REQUIRE_FALSE(any_line_matches(lines, "Projects:"));
    REQUIRE(any_line_matches(lines, "Rating: not rated"));
}

// ---------------------------------------------------------------------------
// Mouse: click a view or an article to select it, wheel to move.
// ---------------------------------------------------------------------------

namespace {
// Screen position of the first cell of `needle`, after a 160x30 render.
std::pair<int, int> where(ArxivApp& app, const std::string& needle) {
    const auto lines = plain_lines(app);
    for (int y = 0; y < static_cast<int>(lines.size()); ++y)
        if (auto x = display_col(lines[static_cast<std::size_t>(y)], needle);
            x != std::string::npos)
            return {static_cast<int>(x), y};
    return {-1, -1};
}

Event mouse(ftxui::Mouse::Button button, int x, int y) {
    ftxui::Mouse m;
    m.button = button;
    m.motion = ftxui::Mouse::Pressed;
    m.x = x;
    m.y = y;
    return Event::Mouse("", m);
}
} // namespace

TEST_CASE("Mouse selects views and articles", "[tui][mouse]") {
    RealDbApp t;
    auto& core = t.app->GetCore();
    auto handler = t.app->GetEventHandler();
    const auto second = core.GetCurrentArticles().at(1);

    SECTION("clicking a view selects it and focuses the filter pane") {
        handler->OnEvent(Event::Character("l")); // focus the article list first
        auto [x, y] = where(*t.app, "Bookmarks");
        REQUIRE(x >= 0);
        handler->OnEvent(mouse(ftxui::Mouse::Left, x + 1, y));
        REQUIRE(core.GetFilterView() == Arxiv::AppCore::FilterView::Bookmarks);
        REQUIRE_THAT(footer_of(*t.app), Catch::Matchers::ContainsSubstring("new project"));
    }
    SECTION("clicking an article selects it and focuses the list") {
        auto [x, y] = where(*t.app, second.title.substr(0, 15));
        REQUIRE(x >= 0);
        handler->OnEvent(mouse(ftxui::Mouse::Left, x + 1, y));
        REQUIRE(core.GetArticleIndex() == 1);
        REQUIRE_THAT(footer_of(*t.app), Catch::Matchers::ContainsSubstring("bookmark"));
    }
    SECTION("the wheel over the list moves the cursor") {
        auto [x, y] = where(*t.app, second.title.substr(0, 15));
        handler->OnEvent(mouse(ftxui::Mouse::WheelDown, x + 1, y));
        REQUIRE(core.GetArticleIndex() == 1);
    }
    SECTION("the wheel over the filters changes view") {
        auto [x, y] = where(*t.app, "Bookmarks");
        handler->OnEvent(mouse(ftxui::Mouse::WheelDown, x + 1, y));
        REQUIRE(core.GetFilterView() == Arxiv::AppCore::FilterView::Bookmarks);
    }
    SECTION("an open dialog keeps clicks from reaching the panes") {
        auto [x, y] = where(*t.app, second.title.substr(0, 15));
        handler->OnEvent(Event::Character("/")); // search dialog
        handler->OnEvent(mouse(ftxui::Mouse::Left, x + 1, y));
        REQUIRE(core.GetArticleIndex() == 0);
    }
}

TEST_CASE("Backspace in a text field removes a whole character", "[tui][utf8][regression]") {
    RealDbApp t;
    auto handler = t.app->GetEventHandler();
    handler->OnEvent(Event::Character("p")); // new project (filter pane)
    handler->OnEvent(Event::Character("z"));
    handler->OnEvent(Event::Character("\xC3\xA9")); // é
    handler->OnEvent(Event::Backspace);
    handler->OnEvent(Event::Return);

    REQUIRE(t.app->GetCore().GetProjects() == std::vector<std::string>{"z"});
}

// ---------------------------------------------------------------------------
// An open dialog gets every key first: global bindings used to fire while the
// user was typing (a for detail, t for categories, / for search, ...).
// ---------------------------------------------------------------------------

namespace {
void type_text(ArxivApp& app, const std::string& s) {
    for (char c : s)
        app.GetEventHandler()->OnEvent(Event::Character(std::string(1, c)));
}
} // namespace

TEST_CASE("Dialogs get keys before the global bindings", "[tui][dialogs][regression]") {
    RealDbApp t;
    auto& core = t.app->GetCore();
    auto handler = t.app->GetEventHandler();

    SECTION("a project name can use any letter") {
        handler->OnEvent(Event::Character("p")); // new project (filter pane)
        type_text(*t.app, "dark matter");
        handler->OnEvent(Event::Return);
        REQUIRE(core.GetProjects() == std::vector<std::string>{"dark matter"});
    }
    SECTION("/ can be typed into a text field") {
        handler->OnEvent(Event::Character("p"));
        type_text(*t.app, "dm/x");
        handler->OnEvent(Event::Return);
        REQUIRE(core.GetProjects() == std::vector<std::string>{"dm/x"});
    }
    SECTION("? is part of a search, not the help") {
        handler->OnEvent(Event::Character("/"));
        type_text(*t.app, "why?");
        handler->OnEvent(Event::Return);
        REQUIRE(core.GetSearchQuery() == "why?");
    }
    SECTION("Escape cancels a new project") {
        handler->OnEvent(Event::Character("p"));
        type_text(*t.app, "zz");
        handler->OnEvent(Event::Escape);
        handler->OnEvent(Event::Return);
        REQUIRE(core.GetProjects().empty());
    }
    SECTION("Space in Assign Project ticks the project, not the article") {
        core.AddProject("P");
        core.SetFilterIndex(Arxiv::AppCore::FilterView::All);
        const auto link = core.GetCurrentArticles().front().link;
        handler->OnEvent(Event::Character("l")); // article list
        handler->OnEvent(Event::Character("p")); // assign to project
        handler->OnEvent(Event::Character(" "));
        handler->OnEvent(Event::Return);
        REQUIRE(core.GetSelectionCount() == 0);
        REQUIRE(core.GetProjectsForArticle(link) == std::vector<std::string>{"P"});
    }
    SECTION("an error message keeps keys from the panes") {
        handler->OnEvent(Event::Character("l"));
        handler->OnEvent(Event::Character("W")); // rate selection with none: error
        handler->OnEvent(Event::Character("j"));
        REQUIRE(core.GetArticleIndex() == 0);
        handler->OnEvent(Event::Return); // dismiss
        handler->OnEvent(Event::Character("j"));
        REQUIRE(core.GetArticleIndex() == 1);
    }
}

// ---------------------------------------------------------------------------
// Settings: a key edited in the Keys section takes effect and is saved under
// its config name; numbers out of range are refused like in config.yml.
// ---------------------------------------------------------------------------

TEST_CASE("Settings: an edited key binding works and is saved by its config name",
          "[tui][settings][regression]") {
    RealDbApp t;
    auto& core = t.app->GetCore();
    auto handler = t.app->GetEventHandler();
    // Where Bookmark sits in the Keys list.
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    const auto all = kb.get_all_bindings();
    const auto row = std::find_if(all.begin(),
                                  all.end(),
                                  [](const auto& b) {
                                      return b.first == KeyBindings::get_action_name(
                                                            KeyBindings::Action::Bookmark);
                                  }) -
                     all.begin();

    handler->OnEvent(Event::Character("S"));
    for (int i = 0; i < 4; ++i)
        handler->OnEvent(Event::Tab); // Keys section
    for (int i = 0; i < row; ++i)
        handler->OnEvent(Event::Character("j"));
    handler->OnEvent(Event::Return); // edit
    handler->OnEvent(Event::Backspace);
    handler->OnEvent(Event::Character("m"));
    handler->OnEvent(Event::Return); // commit
    handler->OnEvent(Event::Escape); // save and close

    SECTION("saved under the config name") {
        const auto mappings = core.GetConfig().get_key_mappings();
        REQUIRE(std::any_of(mappings.begin(), mappings.end(), [](const auto& m) {
            return m.action == "bookmark" && m.key == "m";
        }));
    }
    SECTION("the new key works") {
        handler->OnEvent(Event::Character("l")); // article list
        const auto link = core.GetCurrentArticles().front().link;
        const bool before = core.IsArticleBookmarked(link);
        handler->OnEvent(Event::Character("m"));
        REQUIRE(core.IsArticleBookmarked(link) != before);
    }
}

TEST_CASE("Settings: an out-of-range number is refused", "[tui][settings][regression]") {
    RealDbApp t;
    auto handler = t.app->GetEventHandler();
    const int before = t.app->GetCore().GetConfig().get_retrain_interval();
    handler->OnEvent(Event::Character("S"));
    handler->OnEvent(Event::Tab);
    handler->OnEvent(Event::Tab);            // Ranker section
    handler->OnEvent(Event::Character("j")); // Retrain interval
    handler->OnEvent(Event::Return);
    for (int i = 0; i < 4; ++i)
        handler->OnEvent(Event::Backspace);
    handler->OnEvent(Event::Character("0"));
    handler->OnEvent(Event::Return);
    handler->OnEvent(Event::Escape);

    REQUIRE(t.app->GetCore().GetConfig().get_retrain_interval() == before);
}

TEST_CASE("An article without a title is listed as untitled", "[tui][regression]") {
    // The focused row's title scroller computed `% title.length()`: a
    // division by zero for an empty title (a link-only project import).
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    auto paper = arxiv_tui::test::fixtures::sample_articles[0];
    paper.title.clear();
    db->AddArticle(paper);
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    ArxivApp app(make_test_config(), std::move(db), std::make_unique<FetcherMock>(), std::move(kb));
    app.GetCore().SetFilterIndex(Arxiv::AppCore::FilterView::All);
    app.GetEventHandler()->OnEvent(Event::Character("l")); // focus the list
    app.GetEventHandler()->OnEvent(Event::Character("a")); // detail: title scrolls

    REQUIRE(any_line_matches(plain_lines(app), "\\(no title\\)"));
}

TEST_CASE("Deleting a project asks first", "[tui][projects][regression]") {
    // x removed the project, its memberships and notes at once.
    RealDbApp t;
    auto& core = t.app->GetCore();
    core.AddProject("thesis");
    const auto& options = core.GetFilterOptions();
    const int index =
        static_cast<int>(std::find(options.begin(), options.end(), "thesis") - options.begin());
    core.SetFilterIndex(index);
    auto handler = t.app->GetEventHandler();

    handler->OnEvent(Event::Character("x"));

    SECTION("nothing is deleted yet, and the question names the project") {
        REQUIRE(core.GetProjects() == std::vector<std::string>{"thesis"});
        REQUIRE(any_line_matches(plain_lines(*t.app), "thesis"));
    }
    SECTION("y deletes it") {
        handler->OnEvent(Event::Character("y"));
        REQUIRE(core.GetProjects().empty());
    }
    SECTION("any other key keeps it") {
        handler->OnEvent(Event::Character("n"));
        handler->OnEvent(Event::Character("y"));
        REQUIRE(core.GetProjects() == std::vector<std::string>{"thesis"});
    }
}

// ---------------------------------------------------------------------------
// A failed fetch is visible: the list used to keep showing old papers with
// nothing to say they were not current.
// ---------------------------------------------------------------------------

TEST_CASE("The footer says when arXiv could not be reached", "[tui][fetch]") {
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    for (const auto& a : arxiv_tui::test::fixtures::sample_articles)
        db->AddArticle(a);
    auto fetcher = std::make_unique<FetcherMock>();
    ALLOW_CALL(*fetcher, Fetch()).THROW(Arxiv::FetchError("RSS feed returned HTTP 503"));
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    ArxivApp app(make_test_config(), std::move(db), std::move(fetcher), std::move(kb));

    SECTION("the core reports the failure") {
        REQUIRE(app.GetCore().LastFetchError().find("503") != std::string::npos);
    }
    SECTION("the footer shows it") {
        REQUIRE_THAT(footer_of(app), Catch::Matchers::ContainsSubstring("Offline"));
    }
}

TEST_CASE("No offline notice after a successful fetch", "[tui][fetch]") {
    RealDbApp t;
    REQUIRE(t.app->GetCore().LastFetchError().empty());
    REQUIRE_THAT(footer_of(*t.app), !Catch::Matchers::ContainsSubstring("Offline"));
}
