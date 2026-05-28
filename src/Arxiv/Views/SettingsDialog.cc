// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/App.hh"
#include "Arxiv/Views/Colors.hh"

using namespace ftxui;
using namespace Arxiv;

void ArxivApp::SetupSettingsDialog() {
    // Sections: 0=General 1=Topics 2=Ranker 3=Export 4=Keys
    settings_dialog = Renderer([&] {
        if (dialog_depth != Dialog::Settings)
            return emptyElement();

        static constexpr std::string_view kSectionNames[] = {
            "General", "Topics", "Ranker", "Export", "Keys"};
        static constexpr int kNumSections = 5;

        Elements rows;

        rows.push_back(text(" ── Settings ── ") | bold | color(TextColors::primary()) | center);
        rows.push_back(separator() | color(TextColors::border()));

        {
            Elements tabs;
            tabs.push_back(text(" "));
            for (int i = 0; i < kNumSections; ++i) {
                std::string label = "[" + std::string(kSectionNames[i]) + "]";
                if (i == settings_section)
                    tabs.push_back(text(label) | bold | color(TextColors::base()) |
                                   bgcolor(TextColors::primary()));
                else
                    tabs.push_back(text(label) | color(TextColors::subtext()));
                tabs.push_back(text(" "));
            }
            rows.push_back(hbox(std::move(tabs)));
            rows.push_back(separator() | color(TextColors::border()));
        }

        auto field_row = [&](int idx, std::string_view label, const std::string& value) {
            bool sel = (idx == settings_field_index);
            std::string val = (sel && settings_editing) ? (settings_edit_buffer + "_") : value;
            auto row = hbox({
                text(sel ? "  > " : "    ") | color(TextColors::primary()),
                text(std::string(label)) |
                    color(sel ? TextColors::primary() : TextColors::subtext()),
                text(" : ") | color(TextColors::border()),
                text(val) | color(TextColors::text()),
            });
            if (sel)
                return row | bgcolor(TextColors::surface());
            return row;
        };

        if (settings_section == 0) {
            rows.push_back(field_row(0, "Download dir  ", m_config.get_download_dir()));
            rows.push_back(field_row(
                1, "Auto-refresh  ", std::to_string(m_config.get_auto_refresh_minutes()) + " min"));
            rows.push_back(field_row(
                2, "Scroll margin ", std::to_string(m_config.get_scroll_margin()) + " lines"));

        } else if (settings_section == 1) {
            const auto& topics = m_config.get_topics();
            if (topics.empty()) {
                rows.push_back(text("  (no topics)") | color(TextColors::subtext()));
            } else {
                for (int i = 0; i < static_cast<int>(topics.size()); ++i) {
                    bool sel = (i == settings_field_index);
                    auto row = hbox({
                        text(sel ? "  > " : "    ") | color(TextColors::primary()),
                        text(topics[static_cast<size_t>(i)]) | color(TextColors::text()),
                    });
                    if (sel)
                        rows.push_back(row | bgcolor(TextColors::surface()));
                    else
                        rows.push_back(row);
                }
            }
            rows.push_back(separator() | color(TextColors::border()));
            rows.push_back(hbox({
                text("  ") | color(TextColors::subtext()),
                text("New: " + (settings_editing ? settings_edit_buffer + "_" : "")) |
                    color(TextColors::text()),
            }));

        } else if (settings_section == 2) {
            rows.push_back(field_row(
                0, "Recommend threshold", std::to_string(m_config.get_recommend_threshold())));
            rows.push_back(field_row(1,
                                     "Retrain interval   ",
                                     std::to_string(m_config.get_retrain_interval()) + " ratings"));

        } else if (settings_section == 3) {
            rows.push_back(field_row(0,
                                     "Obsidian vault",
                                     m_config.get_obsidian_vault().empty()
                                         ? "(not set)"
                                         : m_config.get_obsidian_vault()));

        } else if (settings_section == 4) {
            auto bindings = key_bindings.get_all_bindings();
            for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
                bool sel = (i == settings_field_index);
                std::string key_val = (sel && settings_editing)
                                          ? (settings_edit_buffer + "_")
                                          : bindings[static_cast<size_t>(i)].second;
                auto row = hbox({
                    text(sel ? "  > " : "    ") | color(TextColors::primary()),
                    text(bindings[static_cast<size_t>(i)].first) | size(WIDTH, EQUAL, 24) |
                        color(TextColors::subtext()),
                    text(key_val) | color(TextColors::text()),
                });
                if (sel)
                    rows.push_back(row | bgcolor(TextColors::surface()));
                else
                    rows.push_back(row);
            }
        }

        rows.push_back(separator() | color(TextColors::border()));
        rows.push_back(hbox({
            text("j/k") | bold | color(TextColors::primary()),
            text(": move  ") | color(TextColors::subtext()),
            text("Enter") | bold | color(TextColors::primary()),
            text(": edit  ") | color(TextColors::subtext()),
            text("Tab") | bold | color(TextColors::primary()),
            text(": next section  ") | color(TextColors::subtext()),
            text("Del") | bold | color(TextColors::primary()),
            text(": remove (Topics)  ") | color(TextColors::subtext()),
            text("Esc") | bold | color(TextColors::primary()),
            text(": save & close") | color(TextColors::subtext()),
        }));

        return vbox(std::move(rows)) | borderStyled(ROUNDED, TextColors::border()) |
               bgcolor(TextColors::surface()) | clear_under | center;
    });
}

bool ArxivApp::HandleSettingsEvent(ftxui::Event event) {
    static constexpr int kNumSections = 5;

    auto section_fields = [&]() -> int {
        if (settings_section == 0)
            return 3;
        if (settings_section == 1)
            return static_cast<int>(m_config.get_topics().size());
        if (settings_section == 2)
            return 2;
        if (settings_section == 3)
            return 1;
        if (settings_section == 4)
            return static_cast<int>(key_bindings.get_all_bindings().size());
        return 0;
    };

    auto commit_field = [&]() {
        if (settings_section == 0) {
            if (settings_field_index == 0)
                m_config.set_download_dir(settings_edit_buffer);
            else if (settings_field_index == 1) {
                try {
                    m_config.set_auto_refresh_minutes(std::stoi(settings_edit_buffer));
                } catch (...) {
                }
            } else if (settings_field_index == 2) {
                try {
                    m_config.set_scroll_margin(std::stoi(settings_edit_buffer));
                } catch (...) {
                }
            }
        } else if (settings_section == 2) {
            if (settings_field_index == 0) {
                try {
                    m_config.set_recommend_threshold(std::stof(settings_edit_buffer));
                } catch (...) {
                }
            } else if (settings_field_index == 1) {
                try {
                    m_config.set_retrain_interval(std::stoi(settings_edit_buffer));
                } catch (...) {
                }
            }
        } else if (settings_section == 3) {
            if (settings_field_index == 0)
                m_config.set_obsidian_vault(settings_edit_buffer);
        } else if (settings_section == 4) {
            auto bindings = key_bindings.get_all_bindings();
            if (!settings_edit_buffer.empty() &&
                settings_field_index < static_cast<int>(bindings.size())) {
                auto mappings = m_config.get_key_mappings();
                bool found = false;
                for (auto& km : mappings) {
                    if (km.action == bindings[static_cast<size_t>(settings_field_index)].first) {
                        km.key = settings_edit_buffer;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    mappings.push_back({bindings[static_cast<size_t>(settings_field_index)].first,
                                        settings_edit_buffer});
                }
                m_config.set_key_mappings(mappings);
            }
        }
    };

    if (event == Event::Escape) {
        if (settings_editing) {
            commit_field();
            settings_editing = false;
            settings_edit_buffer.clear();
        } else {
            if (!m_config_path.empty())
                m_config.save_to_file(m_config_path);
            core.SetRecommendThreshold(m_config.get_recommend_threshold());
            dialog_depth = Dialog::None;
        }
        return true;
    }

    if (event == Event::Tab) {
        if (settings_editing) {
            commit_field();
            settings_editing = false;
            settings_edit_buffer.clear();
        }
        settings_section = (settings_section + 1) % kNumSections;
        settings_field_index = 0;
        return true;
    }

    if (key_bindings.matches(event, KeyBindings::Action::Next)) {
        if (!settings_editing) {
            int n = section_fields();
            if (n > 0)
                settings_field_index = std::min(settings_field_index + 1, n - 1);
        }
        return true;
    }

    if (key_bindings.matches(event, KeyBindings::Action::Previous)) {
        if (!settings_editing)
            settings_field_index = std::max(0, settings_field_index - 1);
        return true;
    }

    if ((event == Event::Delete) && settings_section == 1 && !settings_editing) {
        auto topics = m_config.get_topics();
        if (!topics.empty() && settings_field_index < static_cast<int>(topics.size())) {
            topics.erase(topics.begin() + settings_field_index);
            m_config.set_topics(topics);
            settings_field_index = std::max(0, settings_field_index - 1);
        }
        return true;
    }

    if (event == Event::Return) {
        if (settings_section == 1 && settings_editing) {
            if (!settings_edit_buffer.empty()) {
                auto topics = m_config.get_topics();
                topics.push_back(settings_edit_buffer);
                m_config.set_topics(topics);
            }
            settings_editing = false;
            settings_edit_buffer.clear();
        } else if (settings_section == 1) {
            settings_edit_buffer.clear();
            settings_editing = true;
        } else if (settings_editing) {
            commit_field();
            settings_editing = false;
            settings_edit_buffer.clear();
        } else {
            if (settings_section == 0) {
                if (settings_field_index == 0)
                    settings_edit_buffer = m_config.get_download_dir();
                else if (settings_field_index == 1)
                    settings_edit_buffer = std::to_string(m_config.get_auto_refresh_minutes());
                else
                    settings_edit_buffer = std::to_string(m_config.get_scroll_margin());
            } else if (settings_section == 2) {
                settings_edit_buffer = (settings_field_index == 0)
                                           ? std::to_string(m_config.get_recommend_threshold())
                                           : std::to_string(m_config.get_retrain_interval());
            } else if (settings_section == 3) {
                settings_edit_buffer = m_config.get_obsidian_vault();
            } else if (settings_section == 4) {
                auto bindings = key_bindings.get_all_bindings();
                if (settings_field_index < static_cast<int>(bindings.size()))
                    settings_edit_buffer =
                        bindings[static_cast<size_t>(settings_field_index)].second;
            }
            settings_editing = true;
        }
        return true;
    }

    if (event == Event::Backspace && settings_editing) {
        if (!settings_edit_buffer.empty())
            settings_edit_buffer.pop_back();
        return true;
    }

    if (event.is_character() && settings_editing) {
        settings_edit_buffer += event.character();
        return true;
    }

    return true;
}
