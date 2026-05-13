#pragma once
#include <array>
#include <string>

// Visual theme parameters for the ImGui GUI frontend.
// No ImGui dependency — colors are stored as RGBA float[4] in [0, 1],
// directly castable to ImVec4.  Layout sizes are in logical pixels.
struct GuiStyle {
    using Color4 = std::array<float, 4>;

    std::string name{"Dark"};

    // --- Layout ---
    float filter_panel_width{200.0f};
    float detail_panel_width{420.0f};
    float row_height_scale{2.2f};   // × GetTextLineHeightWithSpacing()
    float font_size{13.0f};         // saved to config; takes effect on next launch

    // --- Per-role colors (RGBA) ---
    Color4 title_color    {{ 0.40f, 0.80f, 1.00f, 1.00f }};
    Color4 bookmark_color {{ 1.00f, 0.80f, 0.20f, 1.00f }};
    Color4 accent_color   {{ 0.40f, 0.80f, 1.00f, 1.00f }};
    Color4 disabled_color {{ 0.50f, 0.50f, 0.50f, 1.00f }};

    // Built-in preset factories
    static GuiStyle Dark();
    static GuiStyle Light();
    static GuiStyle CatppuccinFrappe();

    // Returns the preset matching name, or Dark() as fallback.
    static GuiStyle from_name(const std::string &name);
};
