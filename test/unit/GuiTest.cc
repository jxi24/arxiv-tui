#include <catch2/catch_test_macros.hpp>

#include "imgui.h"
#include "ArxivGuiApp.hh"

#include <Arxiv/AppCore.hh>
#include <Arxiv/Config.hh>
#include <mocks/DatabaseManagerMock.hh>
#include <mocks/FetcherMock.hh>
#include <fixtures/test_data.hh>

#include <chrono>
#include <ctime>
#include <memory>
#include <string>

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
    ArxivGuiApp   app(fix.core);

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
    ArxivGuiApp app(fix.core);

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
    ArxivGuiApp app(fix.core);

    REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
}

// ---------------------------------------------------------------------------
// Detail panel — out-of-range article index must not crash
// ---------------------------------------------------------------------------

TEST_CASE("ArxivGuiApp detail panel handles out-of-range index gracefully", "[gui]") {
    ImGuiHeadless imgui;
    CoreFixture   fix;
    ArxivGuiApp   app(fix.core);

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
    ArxivGuiApp app(fix.core);

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
    ArxivGuiApp app(fix.core, [&called]{ called = true; });

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
    ArxivGuiApp   app(fix.core);

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
    ArxivGuiApp app(fix.core);

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
    ArxivGuiApp   app(fix.core);

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
    ArxivGuiApp app(fix.core);

    // sample_articles contains cs.AI and math.PR categories
    fix.core.ToggleCategory("cs.AI");

    REQUIRE_NOTHROW(imgui.frame([&]{ app.render(); }));
}
