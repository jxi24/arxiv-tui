// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Config.hh"

#include "Arxiv/AtomicFile.hh"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>

#include "spdlog/spdlog.h"

namespace Arxiv {

Config::Config(const std::string& config_file) { load_from_file(config_file); }

void Config::apply_defaults() {
    article_settings_.topics = {"hep-ph", "hep-ex", "hep-lat", "hep-th"};
    recommend_threshold_ = 3.5f;
    retrain_interval_ = 5;
    key_mappings_ = {
        {"next", "j"},
        {"previous", "k"},
        {"quit", "q"},
        {"create_project", "p"},
        {"delete_project", "x"},
        {"download_article", "d"},
        {"show_detail", "a"},
        {"undo_delete", "u"},
    };
}

void Config::load_from_file(const std::string& config_file) {
    warnings_.clear();
    // Every key is optional: one that is missing or rejected keeps its
    // default (a config without topics used to fetch nothing).
    apply_defaults();
    if (!std::filesystem::exists(config_file)) {
        // Create default configuration; download_dir is left empty so the
        // caller (main) can fill it in from the resolved XDG/install paths.
        apply_defaults();
        save_to_file(config_file);
        return;
    }

    YAML::Node config;
    try {
        config = YAML::LoadFile(config_file);
    } catch (const YAML::Exception& e) {
        // Unreadable as YAML: run on the defaults and say so. The file is
        // left as it is so the user can fix it.
        apply_defaults();
        warnings_.push_back("config.yml could not be read (" + std::string(e.what()) +
                            "); using the defaults for every setting. The file was not changed.");
        spdlog::error("[Config]: {}", warnings_.back());
        return;
    }

    // Valid YAML of the wrong shape (a bare scalar or a list) has no keys;
    // indexing it threw YAML::BadSubscript and aborted startup.
    if (!config.IsNull() && !config.IsMap()) {
        apply_defaults();
        warnings_.push_back("config.yml is not a list of settings (key: value); using the "
                            "defaults for every setting. The file was not changed.");
        spdlog::error("[Config]: {}", warnings_.back());
        return;
    }
    YAML::Node settings_node = config["article_settings"];
    if (settings_node && !settings_node.IsMap()) {
        warnings_.push_back("article_settings: expected download_dir and topics under it; "
                            "using the defaults.");
        spdlog::warn("[Config]: {}", warnings_.back());
        settings_node = YAML::Node();
    }

    // Read one optional key. A value of the wrong type, or one `valid`
    // rejects, keeps the default and is reported in warnings_ (the TUI shows
    // them in a pop-up) instead of aborting startup over a hand-edited config.
    auto read = [&](const YAML::Node& node, const char* key, auto& field, auto valid) {
        if (!node || !node.IsMap() || !node[key])
            return;
        using T = std::decay_t<decltype(field)>;
        const std::string name =
            (&node == &settings_node ? "article_settings." : "") + std::string(key);
        const std::string shown =
            node[key].IsScalar() ? "'" + node[key].Scalar() + "'" : "this value";
        try {
            T value = node[key].template as<T>();
            if (valid(value)) {
                field = std::move(value);
                return;
            }
        } catch (const YAML::Exception&) {
        }
        warnings_.push_back(name + ": " + shown + " is not valid; using the default.");
        spdlog::warn("[Config]: {}", warnings_.back());
    };
    auto any = [](const auto&) { return true; };
    auto positive = [](int v) { return ValidRetrainInterval(v); };
    auto non_negative = [](int v) { return ValidNonNegative(v); };

    read(settings_node, "download_dir", article_settings_.download_dir, any);
    read(settings_node, "topics", article_settings_.topics, any);
    read(config, "recommend_threshold", recommend_threshold_, [](float v) {
        return ValidRecommendThreshold(v);
    });
    read(config, "retrain_interval", retrain_interval_, positive);
    read(config, "auto_refresh_minutes", auto_refresh_minutes_, non_negative);
    read(config, "obsidian_vault", obsidian_vault_, any);
    read(config, "export_dir", export_dir_, any);
    read(config, "keywords_file", keywords_file_, any);
    read(config, "obsidian_download_pdfs", obsidian_download_pdfs_, any);
    read(config, "scroll_margin", scroll_margin_, non_negative);
    read(config, "max_article_age_days", max_article_age_days_, non_negative);
    // Read as a signed number first: "-1" must not wrap to SIZE_MAX.
    long long undo_size = static_cast<long long>(undo_buffer_size_);
    read(config, "undo_buffer_size", undo_size, [](long long v) { return v >= 0 && v <= 1000; });
    undo_buffer_size_ = static_cast<std::size_t>(undo_size);
    read(config, "clipboard_backend", clipboard_backend_, any);
    read(config, "theme", theme_, any);
    read(config, "theme_colors", theme_colors_, any);

    std::vector<std::string> columns;
    read(config, "article_columns", columns, any);
    if (config["article_columns"] && !columns.empty())
        article_columns_ = columns;

    // Load key mappings; malformed entries are skipped individually.
    if (config["key_mappings"] && config["key_mappings"].IsSequence()) {
        key_mappings_.clear();
        for (const auto& mapping : config["key_mappings"]) {
            if (!mapping.IsMap()) {
                warnings_.push_back("key_mappings: an entry is not an action/key pair; "
                                    "skipped.");
                spdlog::warn("[Config]: {}", warnings_.back());
                continue;
            }
            KeyMapping km;
            read(mapping, "action", km.action, any);
            read(mapping, "key", km.key, any);
            if (!km.action.empty() && !km.key.empty())
                key_mappings_.push_back(km);
        }
    }
}

bool Config::save_to_file(const std::string& config_file) const {
    YAML::Node config;

    // Save download settings
    YAML::Node article_settings;
    article_settings["download_dir"] = article_settings_.download_dir;
    article_settings["topics"] = article_settings_.topics;
    config["article_settings"] = article_settings;

    // Save key mappings. An explicit sequence: an empty Node is saved as
    // "~", which loads as "no list" and brings the defaults back.
    YAML::Node key_mappings(YAML::NodeType::Sequence);
    for (const auto& mapping : key_mappings_) {
        YAML::Node mapping_node;
        mapping_node["action"] = mapping.action;
        mapping_node["key"] = mapping.key;
        key_mappings.push_back(mapping_node);
    }
    config["key_mappings"] = key_mappings;

    config["recommend_threshold"] = recommend_threshold_;
    config["retrain_interval"] = retrain_interval_;
    config["auto_refresh_minutes"] = auto_refresh_minutes_;
    config["scroll_margin"] = scroll_margin_;
    config["max_article_age_days"] = max_article_age_days_;
    config["undo_buffer_size"] = undo_buffer_size_;
    config["article_columns"] = article_columns_;
    if (!clipboard_backend_.empty())
        config["clipboard_backend"] = clipboard_backend_;
    config["theme"] = theme_;
    if (!theme_colors_.empty())
        config["theme_colors"] = theme_colors_;
    if (!obsidian_vault_.empty()) {
        config["obsidian_vault"] = obsidian_vault_;
    }
    if (!export_dir_.empty())
        config["export_dir"] = export_dir_;
    config["obsidian_download_pdfs"] = obsidian_download_pdfs_;
    if (!keywords_file_.empty()) {
        config["keywords_file"] = keywords_file_;
    }

    std::ostringstream out;
    out << config;
    return WriteFileAtomically(config_file, out.str());
}

} // namespace Arxiv
