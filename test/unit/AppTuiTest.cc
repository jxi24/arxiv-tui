// Headless TUI tests for ArxivApp using FTXUI's component rendering API.
// Components can receive events and render to a fixed-size screen without
// a real terminal, enabling verification of dialog state and output.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include "Arxiv/App.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/KeyBindings.hh"
#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"

using namespace ftxui;
using namespace Arxiv;
using DatabaseManagerMock = arxiv_tui::test::DatabaseManagerMock;
using FetcherMock         = arxiv_tui::test::FetcherMock;

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
    auto db      = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    KeyBindings kb{std::vector<Config::KeyMapping>{}};
    return std::make_unique<ArxivApp>(std::move(cfg), std::move(db),
                                     std::move(fetcher), std::move(kb));
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

    handler->OnEvent(Event::Character("S"));   // open
    handler->OnEvent(Event::Escape);            // close

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

    handler->OnEvent(Event::Character("S"));   // open settings
    // Navigate to Topics section (Tab moves to next section)
    handler->OnEvent(Event::Tab);

    std::string output = render(*app);
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("hep-ph"));
    REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("cs.LG"));
}
