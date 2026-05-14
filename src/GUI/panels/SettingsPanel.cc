#include "ArxivGuiApp.hh"
#include "imgui.h"

#include <string>

void ArxivGuiApp::render_settings_panel() {
    ImGui::OpenPopup("Settings##dlg");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({600, 520}, ImGuiCond_Always);

    if (!ImGui::BeginPopupModal("Settings##dlg", &m_show_settings,
                                ImGuiWindowFlags_NoResize)) return;

    if (ImGui::BeginTabBar("##stabs")) {
        if (ImGui::BeginTabItem("Appearance")) {
            m_settings_tab = 0;
            render_settings_appearance();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Articles")) {
            m_settings_tab = 1;
            render_settings_articles();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Key Bindings")) {
            m_settings_tab = 2;
            render_settings_keybindings();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Apply", {110, 0}))
        apply_settings();
    ImGui::SameLine();
    if (ImGui::Button("Save", {110, 0})) {
        save_settings();
        m_show_settings = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {110, 0})) {
        m_style = m_config.get_gui_style();
        apply_imgui_base_theme(m_style);
        m_show_settings = false;
    }

    ImGui::EndPopup();
}

void ArxivGuiApp::render_settings_appearance() {
    static const char *kThemes[] = {"Dark", "Light", "Catppuccin Frappe", "Custom"};
    static const int   kNumBuiltin = 3;

    int theme_idx = kNumBuiltin;
    for (int i = 0; i < kNumBuiltin; ++i) {
        if (m_draft_style.name == kThemes[i]) { theme_idx = i; break; }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Theme:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("##theme", &theme_idx, kThemes, IM_ARRAYSIZE(kThemes))) {
        if (theme_idx < kNumBuiltin) {
            m_draft_style = GuiStyle::from_name(kThemes[theme_idx]);
            apply_imgui_base_theme(m_draft_style);
        } else {
            m_draft_style.name = "Custom";
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Filter panel width:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("##fpw", &m_draft_style.filter_panel_width, 100.0f, 400.0f, "%.0f px");

    ImGui::Text("Detail panel width:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("##dpw", &m_draft_style.detail_panel_width, 200.0f, 700.0f, "%.0f px");

    ImGui::Text("Row height scale:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("##rhs", &m_draft_style.row_height_scale, 1.0f, 5.0f, "%.1f x");

    ImGui::Text("Font size:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("##fs", &m_draft_style.font_size, 8.0f, 32.0f, "%.0f pt");
    ImGui::SameLine();
    ImGui::TextDisabled("(restart to apply)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Per-role colours:");
    ImGui::Spacing();

    auto color_edit = [&](const char *label, GuiStyle::Color4 &c) {
        float rgba[4] = {c[0], c[1], c[2], c[3]};
        ImGui::Text("%s", label);
        ImGui::SameLine(130);
        if (ImGui::ColorEdit4(("##" + std::string(label)).c_str(), rgba,
                              ImGuiColorEditFlags_NoInputs)) {
            c = {rgba[0], rgba[1], rgba[2], rgba[3]};
            m_draft_style.name = "Custom";
        }
    };
    color_edit("Title colour",    m_draft_style.title_color);
    color_edit("Bookmark colour", m_draft_style.bookmark_color);
    color_edit("Accent colour",   m_draft_style.accent_color);
    color_edit("Disabled colour", m_draft_style.disabled_color);
}

void ArxivGuiApp::render_settings_articles() {
    ImGui::Text("Download dir:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##dd", m_draft_download_dir, sizeof(m_draft_download_dir));

    ImGui::Text("Auto-refresh:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("##ar", &m_draft_auto_refresh);
    ImGui::SameLine();
    ImGui::TextDisabled("minutes (0 = off)");

    ImGui::Text("Score threshold:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("##thr", &m_draft_threshold, 0.0f, 10.0f, "%.1f");

    ImGui::Text("Retrain every:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("##ret", &m_draft_retrain);
    ImGui::SameLine();
    ImGui::TextDisabled("fetches");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Topics (arXiv category IDs):");
    ImGui::Spacing();

    for (int i = 0; i < static_cast<int>(m_draft_topics.size()); ++i) {
        ImGui::BulletText("%s", m_draft_topics[static_cast<size_t>(i)].c_str());
        ImGui::SameLine(250);
        ImGui::PushID(i);
        if (ImGui::SmallButton("Remove"))
            m_draft_topics.erase(m_draft_topics.begin() + i);
        ImGui::PopID();
    }

    ImGui::SetNextItemWidth(140);
    ImGui::InputText("##newtopic", m_draft_new_topic, sizeof(m_draft_new_topic));
    ImGui::SameLine();
    if (ImGui::Button("Add topic") && m_draft_new_topic[0] != '\0') {
        m_draft_topics.emplace_back(m_draft_new_topic);
        m_draft_new_topic[0] = '\0';
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Keywords file:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##kw", m_draft_keywords, sizeof(m_draft_keywords));

    ImGui::Text("Obsidian vault:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##obs", m_draft_obsidian, sizeof(m_draft_obsidian));

    ImGui::Text("Ranker file:");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##rnk", m_draft_ranker, sizeof(m_draft_ranker));
}

void ArxivGuiApp::render_settings_keybindings() {
    constexpr float COL_ACTION = 250.0f;

    ImGui::TextDisabled("Click a binding to capture a new key.");
    ImGui::Spacing();

    auto has_conflict = [&](int idx) {
        const auto &k = m_draft_keys[static_cast<size_t>(idx)].key;
        for (int j = 0; j < static_cast<int>(m_draft_keys.size()); ++j) {
            if (j != idx && m_draft_keys[static_cast<size_t>(j)].key == k)
                return true;
        }
        return false;
    };

    if (ImGui::BeginTable("##kbtbl", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action",  ImGuiTableColumnFlags_WidthFixed, COL_ACTION);
        ImGui::TableSetupColumn("Key",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(m_draft_keys.size()); ++i) {
            auto &km = m_draft_keys[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(km.action.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(i);
            bool capturing = (m_capturing_action == km.action);
            bool conflict  = has_conflict(i);

            if (conflict)
                ImGui::PushStyleColor(ImGuiCol_Button, {0.8f, 0.2f, 0.2f, 1.0f});
            else if (capturing)
                ImGui::PushStyleColor(ImGuiCol_Button, {0.2f, 0.6f, 0.2f, 1.0f});

            const std::string label = capturing ? "[press key\xe2\x80\xa6]" : km.key;
            if (ImGui::Button(label.c_str(), {-1, 0}))
                m_capturing_action = capturing ? "" : km.action;

            if (conflict || capturing) ImGui::PopStyleColor();

            if (capturing) {
                for (ImGuiKey key = ImGuiKey_A; key <= ImGuiKey_Z;
                     key = static_cast<ImGuiKey>(key + 1)) {
                    if (ImGui::IsKeyPressed(key, false)) {
                        const char letter[2] = {
                            static_cast<char>('a' + (key - ImGuiKey_A)), '\0'};
                        km.key = letter;
                        m_capturing_action.clear();
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    m_capturing_action.clear();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
