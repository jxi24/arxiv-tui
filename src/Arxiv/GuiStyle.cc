#include "Arxiv/GuiStyle.hh"

GuiStyle GuiStyle::Dark() {
    // Struct defaults already define the Dark theme.
    return GuiStyle{};
}

GuiStyle GuiStyle::Light() {
    GuiStyle s;
    s.name         = "Light";
    s.title_color    = {{ 0.10f, 0.40f, 0.70f, 1.00f }};
    s.bookmark_color = {{ 0.75f, 0.45f, 0.00f, 1.00f }};
    s.accent_color   = {{ 0.10f, 0.40f, 0.70f, 1.00f }};
    s.disabled_color = {{ 0.35f, 0.35f, 0.35f, 1.00f }};
    return s;
}

GuiStyle GuiStyle::CatppuccinFrappe() {
    GuiStyle s;
    s.name         = "Catppuccin Frappe";
    // Blue #8caaee, Yellow/Gold #e5c890, Subtext0 #a5adce
    s.title_color    = {{ 0.549f, 0.667f, 0.933f, 1.00f }};
    s.bookmark_color = {{ 0.898f, 0.784f, 0.565f, 1.00f }};
    s.accent_color   = {{ 0.549f, 0.667f, 0.933f, 1.00f }};
    s.disabled_color = {{ 0.647f, 0.678f, 0.808f, 1.00f }};
    return s;
}

GuiStyle GuiStyle::from_name(const std::string &name) {
    if (name == "Light")             return Light();
    if (name == "Catppuccin Frappe") return CatppuccinFrappe();
    return Dark();
}
