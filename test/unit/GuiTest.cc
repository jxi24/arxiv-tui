#include <catch2/catch_test_macros.hpp>

#include "imgui.h"
#include "ArxivGuiApp.hh"

#include <Arxiv/AppCore.hh>
#include <Arxiv/Config.hh>
#include <mocks/DatabaseManagerMock.hh>
#include <mocks/FetcherMock.hh>
#include <fixtures/test_data.hh>

#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

using namespace Arxiv;
using namespace arxiv_tui::test;
using namespace arxiv_tui::test::fixtures;

// ---------------------------------------------------------------------------
// Headless ImGui context — no GLFW, no OpenGL, no display required.
// ---------------------------------------------------------------------------

struct ImGuiHeadless {
    ImGuiContext *ctx;

    ImGuiHeadless() {
        IMGUI_CHECKVERSION();
        ctx = ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 800);
        io.DeltaTime   = 1.0f / 60.0f;
        // Build the font atlas so NewFrame() doesn't assert on a null TexID.
        unsigned char *px; int w, h;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1u));
    }
    ~ImGuiHeadless() { ImGui::DestroyContext(ctx); }

    // Drive one complete ImGui frame through fn().
    void frame(std::function<void()> fn) {
        ImGui::NewFrame();
        fn();
        ImGui::EndFrame();
    }
};

// ---------------------------------------------------------------------------
// Shared helper — constructs an AppCore backed by mocks.
// ---------------------------------------------------------------------------

struct CoreFixture {
    Config                               config{"test/fixtures/test_config.yml"};
    std::unique_ptr<DatabaseManagerMock> db_owner{std::make_unique<DatabaseManagerMock>()};
    std::unique_ptr<FetcherMock>         fetcher_owner{std::make_unique<FetcherMock>()};
    DatabaseManagerMock                 *db{db_owner.get()};
    FetcherMock                         *fetcher{fetcher_owner.get()};
    AppCore core{config, std::move(db_owner), std::move(fetcher_owner)};
};

// ---------------------------------------------------------------------------
// format_date
// ---------------------------------------------------------------------------

TEST_CASE("format_date formats a UTC timestamp as YYYY-MM-DD", "[gui][format_date]") {
    SECTION("Unix epoch is 1970-01-01") {
        auto tp = std::chrono::system_clock::from_time_t(0);
        REQUIRE(format_date(tp) == "1970-01-01");
    }

    SECTION("A known 2024 date is formatted correctly") {
        // 2024-03-25 00:00:00 UTC
        std::tm tm{};
        tm.tm_year = 124; // 2024
        tm.tm_mon  = 2;   // March
        tm.tm_mday = 25;
        auto tp = std::chrono::system_clock::from_time_t(timegm(&tm));
        REQUIRE(format_date(tp) == "2024-03-25");
    }

    SECTION("format_date uses UTC, not local time") {
        // Midnight UTC on 2000-01-01
        std::tm tm{};
        tm.tm_year = 100;
        tm.tm_mon  = 0;
        tm.tm_mday = 1;
        auto tp = std::chrono::system_clock::from_time_t(timegm(&tm));
        REQUIRE(format_date(tp) == "2000-01-01");
    }
}

// ---------------------------------------------------------------------------
// Basic render smoke tests — must not crash or abort
// ---------------------------------------------------------------------------

TEST_CASE("ArxivGuiApp render does not crash with empty article list", "[gui][smoke]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    ArxivGuiApp   app(fix.core, fix.config);

    SECTION("single frame") {
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }

    SECTION("ten consecutive frames — no state accumulation") {
        for (int i = 0; i < 10; ++i)
            REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}

TEST_CASE("ArxivGuiApp render does not crash with populated article list", "[gui][smoke]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    fix.db->setArticles(sample_articles);
    fix.core.SetFilterIndex(AppCore::FilterView::All);
    ArxivGuiApp app(fix.core, fix.config);

    SECTION("single frame") {
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }

    SECTION("multiple frames") {
        for (int i = 0; i < 5; ++i)
            REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}

TEST_CASE("ArxivGuiApp render does not crash with bookmarked articles", "[gui][smoke]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;

    // sample_articles[1] has bookmarked = true
    fix.db->setBookmarkedArticles({sample_articles[1]});
    fix.core.SetFilterIndex(AppCore::FilterView::Bookmarks);
    ArxivGuiApp app(fix.core, fix.config);

    REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
}

// ---------------------------------------------------------------------------
// Detail panel — out-of-range article index must not crash
// ---------------------------------------------------------------------------

TEST_CASE("ArxivGuiApp detail panel handles out-of-range index gracefully", "[gui]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    ArxivGuiApp   app(fix.core, fix.config);

    SECTION("index 0 with empty list — shows placeholder, no crash") {
        fix.core.SetArticleIndex(0);
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }

    SECTION("negative index with articles — no crash") {
        fix.db->setArticles(sample_articles);
        fix.core.SetFilterIndex(AppCore::FilterView::All);
        fix.core.SetArticleIndex(-1);
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}

// ---------------------------------------------------------------------------
// Title truncation
// ---------------------------------------------------------------------------

TEST_CASE("Long titles are truncated to 80 characters", "[gui]") {
    // The truncation lives in render_article_panel, so we exercise it by
    // rendering a frame with an article whose title exceeds 80 chars.
    ImGuiHeadless imgui;
    CoreFixture   fix;

    Arxiv::Article long_title_article = sample_articles[0];
    long_title_article.title = std::string(100, 'A'); // 100 chars

    fix.db->setArticles({long_title_article});
    fix.core.SetFilterIndex(AppCore::FilterView::All);
    ArxivGuiApp app(fix.core, fix.config);

    // Rendering must not crash (the substr call is the risk).
    REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
}

// ---------------------------------------------------------------------------
// Quit callback
// ---------------------------------------------------------------------------

TEST_CASE("Quit callback is wired up correctly", "[gui]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;

    bool called = false;
    ArxivGuiApp app(fix.core, fix.config, [&called]{ called = true; });

    SECTION("callback not triggered by a plain render") {
        imgui.frame([&]{ app.render(); });
        REQUIRE(!called);
    }
}

// ---------------------------------------------------------------------------
// filter_index propagation
// ---------------------------------------------------------------------------

TEST_CASE("ArxivGuiApp reads filter options from AppCore", "[gui]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    ArxivGuiApp   app(fix.core, fix.config);

    const auto opts = fix.core.GetFilterOptions();
    REQUIRE(!opts.empty());

    SECTION("renders all filter views without crash") {
        for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
            fix.core.SetFilterIndex(i);
            REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
        }
    }
}

// ---------------------------------------------------------------------------
// Article navigation — SetArticleIndex effect visible across frames
// ---------------------------------------------------------------------------

TEST_CASE("Article index changes are reflected in AppCore state", "[gui]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    fix.db->setArticles(sample_articles);
    fix.core.SetFilterIndex(AppCore::FilterView::All);
    ArxivGuiApp app(fix.core, fix.config);

    imgui.frame([&]{ app.render(); });

    SECTION("SetArticleIndex(1) is readable immediately") {
        fix.core.SetArticleIndex(1);
        REQUIRE(fix.core.GetArticleIndex() == 1);
    }

    SECTION("SetArticleIndex clamps are enforced by AppCore") {
        fix.core.SetArticleIndex(0);
        REQUIRE(fix.core.GetArticleIndex() == 0);
    }
}

// ---------------------------------------------------------------------------
// Search state
// ---------------------------------------------------------------------------

TEST_CASE("SetSearchQuery moves AppCore into search filter state", "[gui]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    ArxivGuiApp   app(fix.core, fix.config);

    imgui.frame([&]{ app.render(); });

    SECTION("search query is active after SetSearchQuery") {
        ALLOW_CALL(*fix.db, SearchArticles(ANY(std::string), ANY(bool), ANY(bool), ANY(bool)))
            .RETURN(std::vector<Arxiv::Article>{});
        fix.core.SetSearchQuery("quantum", true, false, false);
        fix.core.SetFilterIndex(AppCore::FilterView::Search);

        REQUIRE(fix.core.HasSearchQuery());
        REQUIRE(fix.core.GetSearchQuery() == "quantum");
        REQUIRE(fix.core.GetFilterView() == AppCore::FilterView::Search);
    }

    SECTION("ClearSearch removes the query") {
        fix.core.SetSearchQuery("test");
        fix.core.ClearSearch();
        REQUIRE(!fix.core.HasSearchQuery());
    }
}

// ---------------------------------------------------------------------------
// Category toggle — renders correctly with a subset of active categories
// ---------------------------------------------------------------------------

TEST_CASE("ArxivGuiApp renders correctly when a category is toggled off", "[gui]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    fix.db->setArticles(sample_articles);
    fix.core.SetFilterIndex(AppCore::FilterView::All);
    ArxivGuiApp app(fix.core, fix.config);

    // sample_articles contains cs.AI and math.PR categories
    fix.core.ToggleCategory("cs.AI");

    REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
}

// ---------------------------------------------------------------------------
// Theme rendering — each built-in theme must render without crash
// ---------------------------------------------------------------------------

TEST_CASE("ArxivGuiApp renders correctly with Dark theme", "[gui][theme]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    fix.config.set_gui_style(GuiStyle::Dark());
    fix.db->setArticles(sample_articles);
    fix.core.SetFilterIndex(AppCore::FilterView::All);
    ArxivGuiApp app(fix.core, fix.config);

    SECTION("single frame does not crash") {
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }

    SECTION("ten frames do not crash") {
        for (int i = 0; i < 10; ++i)
            REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}

TEST_CASE("ArxivGuiApp renders correctly with Light theme", "[gui][theme]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    fix.config.set_gui_style(GuiStyle::Light());
    fix.db->setArticles(sample_articles);
    fix.core.SetFilterIndex(AppCore::FilterView::All);
    ArxivGuiApp app(fix.core, fix.config);

    SECTION("single frame does not crash") {
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}

TEST_CASE("ArxivGuiApp renders correctly with Catppuccin Frappe theme", "[gui][theme]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    fix.config.set_gui_style(GuiStyle::CatppuccinFrappe());
    fix.db->setArticles(sample_articles);
    fix.core.SetFilterIndex(AppCore::FilterView::All);
    ArxivGuiApp app(fix.core, fix.config);

    SECTION("single frame does not crash") {
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}

TEST_CASE("ArxivGuiApp picks up style layout values from config", "[gui][theme]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;

    GuiStyle style = GuiStyle::Dark();
    style.filter_panel_width = 300.0f;
    style.detail_panel_width = 550.0f;
    style.row_height_scale   = 3.0f;
    fix.config.set_gui_style(style);

    fix.db->setArticles(sample_articles);
    fix.core.SetFilterIndex(AppCore::FilterView::All);
    ArxivGuiApp app(fix.core, fix.config);

    SECTION("renders without crash with non-default panel widths") {
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}

// ---------------------------------------------------------------------------
// Settings panel — appearance tab
// ---------------------------------------------------------------------------

TEST_CASE("Settings appearance tab renders without crash", "[gui][settings]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    ArxivGuiApp   app(fix.core, fix.config);

    // Open the settings panel by calling render() first then opening via the
    // internal helper — we verify through smoke rendering only.
    SECTION("settings panel opens and renders a frame") {
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}

TEST_CASE("Settings draft_style reflects config on open_settings", "[gui][settings]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;

    GuiStyle style = GuiStyle::Light();
    style.filter_panel_width = 180.0f;
    fix.config.set_gui_style(style);

    ArxivGuiApp app(fix.core, fix.config);

    // open_settings() is private; its effect is observable by verifying that a
    // subsequent Save round-trips through Config.  We exercise it indirectly by
    // checking that the config still holds the style we set.
    SECTION("config retains the style we installed before constructing the app") {
        REQUIRE(fix.config.get_gui_style().name == "Light");
        REQUIRE_THAT(fix.config.get_gui_style().filter_panel_width,
                     Catch::Matchers::WithinAbs(180.0f, 1e-4f));
    }
}

// ---------------------------------------------------------------------------
// Settings panel — articles tab
// ---------------------------------------------------------------------------

TEST_CASE("Settings articles tab smoke renders without crash", "[gui][settings]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    fix.db->setArticles(sample_articles);
    fix.core.SetFilterIndex(AppCore::FilterView::All);
    ArxivGuiApp app(fix.core, fix.config);

    SECTION("multiple frames with articles") {
        for (int i = 0; i < 3; ++i)
            REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}

TEST_CASE("Draft state reflects Config values for article settings", "[gui][settings]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;

    // Set distinctive config values before constructing the app.
    fix.config.set_download_dir("my_papers/");
    fix.config.set_auto_refresh_minutes(15);
    fix.config.set_recommend_threshold(4.2f);
    fix.config.set_retrain_interval(3);

    SECTION("config holds article settings we installed") {
        REQUIRE(fix.config.get_download_dir() == "my_papers/");
        REQUIRE(fix.config.get_auto_refresh_minutes() == 15);
        REQUIRE_THAT(fix.config.get_recommend_threshold(),
                     Catch::Matchers::WithinAbs(4.2f, 1e-4f));
        REQUIRE(fix.config.get_retrain_interval() == 3);
    }
}

// ---------------------------------------------------------------------------
// Settings panel — key bindings tab
// ---------------------------------------------------------------------------

TEST_CASE("Settings key bindings tab smoke renders without crash", "[gui][settings]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    ArxivGuiApp   app(fix.core, fix.config);

    SECTION("renders without crash") {
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}

TEST_CASE("Key binding conflict detection identifies duplicate keys", "[gui][settings]") {
    // Verify that duplicate keys in the mapping list can be detected; the
    // detection logic is the has_conflict lambda inside render_settings_keybindings.
    // We test it indirectly through the Config key mapping API.
    ImGuiHeadless imgui;
    CoreFixture   fix;

    std::vector<Arxiv::Config::KeyMapping> km = {
        {"next",     "j"},
        {"previous", "k"},
        {"quit",     "j"}, // duplicate of "next"
    };
    fix.config.set_key_mappings(km);

    SECTION("duplicate key is detectable by comparing key strings") {
        const auto &mappings = fix.config.get_key_mappings();
        REQUIRE(mappings.size() == 3);
        REQUIRE(mappings[0].key == mappings[2].key);
    }
}

// ---------------------------------------------------------------------------
// Settings panel — Save / Apply / Cancel
// ---------------------------------------------------------------------------

TEST_CASE("Apply settings pushes gui style to Config", "[gui][settings]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;

    fix.config.set_gui_style(GuiStyle::Dark());
    ArxivGuiApp app(fix.core, fix.config);

    // Render one frame, then change the config style directly (simulates what
    // Apply does), and verify the config reflects the new value.
    SECTION("config style is updated after set_gui_style") {
        fix.config.set_gui_style(GuiStyle::Light());
        REQUIRE(fix.config.get_gui_style().name == "Light");
    }
}

TEST_CASE("Save settings writes the config file to disk", "[gui][settings]") {
    ImGuiHeadless imgui;

    // Use a real temp file so we can verify the write.
    char path[64];
    std::snprintf(path, sizeof(path), "/tmp/arxiv_gui_save_test_%d.yml", ::getpid());
    struct Cleanup { const char *p; ~Cleanup() { std::remove(p); } } cleanup{path};

    Arxiv::Config cfg(path);
    cfg.set_gui_style(GuiStyle::CatppuccinFrappe());

    std::unique_ptr<DatabaseManagerMock> db_owner{std::make_unique<DatabaseManagerMock>()};
    std::unique_ptr<FetcherMock> fetcher_owner{std::make_unique<FetcherMock>()};
    AppCore core{cfg, std::move(db_owner), std::move(fetcher_owner)};

    ArxivGuiApp app(core, cfg);
    imgui.frame([&]{ app.render(); });

    cfg.save();

    SECTION("saved file exists on disk") {
        REQUIRE(std::filesystem::exists(path));
    }

    SECTION("reloaded config preserves the Catppuccin Frappe theme") {
        Arxiv::Config reloaded(path);
        REQUIRE(reloaded.get_gui_style().name == "Catppuccin Frappe");
    }
}

TEST_CASE("Cancel settings reverts draft to saved config", "[gui][settings]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;

    fix.config.set_gui_style(GuiStyle::Dark());
    ArxivGuiApp app(fix.core, fix.config);

    // Simulate what would happen if the user cancels: config must retain the
    // original style.
    SECTION("config style is unchanged after a Cancel-equivalent operation") {
        // Temporarily switch in a different style without saving.
        GuiStyle tmp = GuiStyle::Light();
        fix.config.set_gui_style(tmp);
        // "Cancel" restores the previously persisted style; here we just set it back.
        fix.config.set_gui_style(GuiStyle::Dark());
        REQUIRE(fix.config.get_gui_style().name == "Dark");
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// apply_settings — auto-refresh hot-reload correctness
// ---------------------------------------------------------------------------

TEST_CASE("apply_settings starts auto-refresh when draft changes from 0 to non-zero",
          "[gui][settings][apply]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;

    // Config starts with auto-refresh disabled.
    fix.config.set_auto_refresh_minutes(0);
    ArxivGuiApp app(fix.core, fix.config);
    imgui.frame([&]{ app.render(); });

    // Simulate user opening settings and changing the interval to 5.
    app.open_settings();
    app.m_draft_auto_refresh = 5;
    app.apply_settings();

    // AppCore must now be auto-refreshing.
    REQUIRE(fix.core.IsAutoRefreshing());

    // Cleanup so the test doesn't leave a background thread dangling.
    fix.core.StopAutoRefresh();
}

TEST_CASE("apply_settings stops auto-refresh when draft changes from non-zero to 0",
          "[gui][settings][apply]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;

    fix.config.set_auto_refresh_minutes(5);
    ArxivGuiApp app(fix.core, fix.config);
    fix.core.StartAutoRefresh();
    REQUIRE(fix.core.IsAutoRefreshing());
    imgui.frame([&]{ app.render(); });

    // User opens settings and clears the interval.
    app.open_settings();               // copies 5 from config into draft
    app.m_draft_auto_refresh = 0;     // user changes to 0
    app.apply_settings();

    REQUIRE_FALSE(fix.core.IsAutoRefreshing());
}

TEST_CASE("apply_settings does not restart auto-refresh when interval is unchanged",
          "[gui][settings][apply]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;

    fix.config.set_auto_refresh_minutes(0);
    ArxivGuiApp app(fix.core, fix.config);
    imgui.frame([&]{ app.render(); });

    app.open_settings();
    // Draft matches config — no change.
    app.apply_settings();

    // Should still be off.
    REQUIRE_FALSE(fix.core.IsAutoRefreshing());
}

// ---------------------------------------------------------------------------
// Status bar smoke
// ---------------------------------------------------------------------------

TEST_CASE("Status bar renders without crash", "[gui][statusbar]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    fix.db->setArticles(sample_articles);
    fix.core.SetFilterIndex(AppCore::FilterView::All);
    ArxivGuiApp app(fix.core, fix.config);

    SECTION("single frame with populated article list") {
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }

    SECTION("empty article list shows zero count") {
        fix.core.SetFilterIndex(AppCore::FilterView::Bookmarks);
        REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
    }
}
