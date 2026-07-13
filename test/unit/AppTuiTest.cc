// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Headless TUI tests for ArxivApp using FTXUI's component rendering API.
// Components can receive events and render to a fixed-size screen without
// a real terminal, enabling verification of dialog state and output.

#include "Arxiv/App.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/KeyBindings.hh"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

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
