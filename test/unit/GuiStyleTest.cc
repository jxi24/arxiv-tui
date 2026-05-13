#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Arxiv/Config.hh>
#include <Arxiv/GuiStyle.hh>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// GuiStyle preset factories
// ---------------------------------------------------------------------------

TEST_CASE("GuiStyle::Dark returns default-named Dark style", "[guistyle]") {
    const auto s = GuiStyle::Dark();
    REQUIRE(s.name == "Dark");
    REQUIRE_THAT(s.title_color[0], WithinAbs(0.40f, 1e-4f));
}

TEST_CASE("GuiStyle::Light returns Light style with distinct colors", "[guistyle]") {
    const auto dark  = GuiStyle::Dark();
    const auto light = GuiStyle::Light();
    REQUIRE(light.name == "Light");
    // Title color must differ from Dark
    REQUIRE(light.title_color[0] != dark.title_color[0]);
}

TEST_CASE("GuiStyle::CatppuccinFrappe returns correct name", "[guistyle]") {
    const auto s = GuiStyle::CatppuccinFrappe();
    REQUIRE(s.name == "Catppuccin Frappe");
    REQUIRE_THAT(s.title_color[0], WithinAbs(0.549f, 1e-3f));
}

TEST_CASE("GuiStyle::from_name round-trips known names", "[guistyle]") {
    SECTION("Dark") {
        REQUIRE(GuiStyle::from_name("Dark").name == "Dark");
    }
    SECTION("Light") {
        REQUIRE(GuiStyle::from_name("Light").name == "Light");
    }
    SECTION("Catppuccin Frappe") {
        REQUIRE(GuiStyle::from_name("Catppuccin Frappe").name == "Catppuccin Frappe");
    }
    SECTION("Unknown name falls back to Dark") {
        REQUIRE(GuiStyle::from_name("DoesNotExist").name == "Dark");
    }
}

// ---------------------------------------------------------------------------
// Config GuiStyle round-trip
// ---------------------------------------------------------------------------

namespace {
struct TmpFile {
    std::string path;
    explicit TmpFile(const char *suffix) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "/tmp/arxiv_cfg_test_%d%s", ::getpid(), suffix);
        path = buf;
    }
    ~TmpFile() { std::remove(path.c_str()); }
};
} // namespace

TEST_CASE("Config saves and reloads GuiStyle faithfully", "[guistyle][config]") {
    TmpFile tmp(".yml");

    // Build a non-default style
    GuiStyle style = GuiStyle::CatppuccinFrappe();
    style.font_size          = 16.0f;
    style.filter_panel_width = 250.0f;
    style.detail_panel_width = 500.0f;
    style.row_height_scale   = 2.8f;
    style.title_color        = {{ 0.1f, 0.2f, 0.3f, 1.0f }};
    style.bookmark_color     = {{ 0.9f, 0.8f, 0.7f, 1.0f }};
    style.accent_color       = {{ 0.4f, 0.5f, 0.6f, 1.0f }};
    style.disabled_color     = {{ 0.3f, 0.3f, 0.3f, 1.0f }};

    {
        Arxiv::Config cfg(tmp.path);  // creates defaults on first load
        cfg.set_gui_style(style);
        cfg.save();
    }

    Arxiv::Config reloaded(tmp.path);
    const GuiStyle &r = reloaded.get_gui_style();

    SECTION("theme name preserved") {
        REQUIRE(r.name == "Catppuccin Frappe");
    }
    SECTION("layout values preserved") {
        REQUIRE_THAT(r.font_size,          WithinAbs(16.0f, 1e-4f));
        REQUIRE_THAT(r.filter_panel_width, WithinAbs(250.0f, 1e-4f));
        REQUIRE_THAT(r.detail_panel_width, WithinAbs(500.0f, 1e-4f));
        REQUIRE_THAT(r.row_height_scale,   WithinAbs(2.8f,  1e-4f));
    }
    SECTION("per-role colors preserved") {
        REQUIRE_THAT(r.title_color[0],    WithinAbs(0.1f, 1e-4f));
        REQUIRE_THAT(r.bookmark_color[0], WithinAbs(0.9f, 1e-4f));
        REQUIRE_THAT(r.accent_color[2],   WithinAbs(0.6f, 1e-4f));
        REQUIRE_THAT(r.disabled_color[0], WithinAbs(0.3f, 1e-4f));
    }
}

TEST_CASE("Config round-trips Dark style without gui key present", "[guistyle][config]") {
    TmpFile tmp(".yml");
    {
        Arxiv::Config cfg(tmp.path);
        // Dark is the default; save without explicitly setting gui style
        cfg.save();
    }
    Arxiv::Config reloaded(tmp.path);
    REQUIRE(reloaded.get_gui_style().name == "Dark");
}

TEST_CASE("Config save() uses constructor path", "[guistyle][config]") {
    TmpFile tmp(".yml");
    Arxiv::Config cfg(tmp.path);
    cfg.set_gui_style(GuiStyle::Light());
    cfg.save();   // should write to tmp.path

    REQUIRE(std::filesystem::exists(tmp.path));
    Arxiv::Config reloaded(tmp.path);
    REQUIRE(reloaded.get_gui_style().name == "Light");
}

TEST_CASE("Config get_config_file returns constructor path", "[guistyle][config]") {
    TmpFile tmp(".yml");
    Arxiv::Config cfg(tmp.path);
    REQUIRE(cfg.get_config_file() == tmp.path);
}
