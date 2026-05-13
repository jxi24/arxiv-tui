#include "Arxiv/Config.hh"
#include <filesystem>
#include <fstream>

namespace Arxiv {

Config::Config(const std::string &config_file) : config_file_(config_file) {
    load_from_file(config_file);
}

void Config::load_from_file(const std::string &config_file) {
    config_file_ = config_file;

    if (!std::filesystem::exists(config_file)) {
        article_settings_.download_dir = "downloads";
        article_settings_.topics = {"hep-ph", "hep-ex", "hep-lat", "hep-th"};
        recommend_threshold_ = 3.5f;
        retrain_interval_    = 5;
        key_mappings_ = {
            {"next",            "j"},
            {"previous",        "k"},
            {"quit",            "q"},
            {"create_project",  "p"},
            {"delete_project",  "x"},
            {"download_article","d"},
            {"show_detail",     "a"},
        };
        save_to_file(config_file);
        return;
    }

    YAML::Node config = YAML::LoadFile(config_file);

    if (config["article_settings"]) {
        const auto &ds = config["article_settings"];
        article_settings_.download_dir = ds["download_dir"].as<std::string>();
        article_settings_.topics = ds["topics"].as<std::vector<std::string>>();
    }

    if (config["recommend_threshold"])
        recommend_threshold_ = config["recommend_threshold"].as<float>();
    if (config["retrain_interval"])
        retrain_interval_ = config["retrain_interval"].as<int>();
    if (config["obsidian_vault"])
        obsidian_vault_ = config["obsidian_vault"].as<std::string>();
    if (config["auto_refresh_minutes"])
        auto_refresh_minutes_ = config["auto_refresh_minutes"].as<int>();
    if (config["keywords_file"])
        keywords_file_ = config["keywords_file"].as<std::string>();
    if (config["ranker_file"])
        ranker_file_ = config["ranker_file"].as<std::string>();

    if (config["key_mappings"]) {
        key_mappings_.clear();
        for (const auto &m : config["key_mappings"]) {
            KeyMapping km;
            km.action = m["action"].as<std::string>();
            km.key    = m["key"].as<std::string>();
            key_mappings_.push_back(km);
        }
    }

    // --- GUI style ---
    if (config["gui"]) {
        const auto &g = config["gui"];
        if (g["theme"])
            gui_style_ = GuiStyle::from_name(g["theme"].as<std::string>());
        if (g["font_size"])
            gui_style_.font_size          = g["font_size"].as<float>();
        if (g["filter_panel_width"])
            gui_style_.filter_panel_width = g["filter_panel_width"].as<float>();
        if (g["detail_panel_width"])
            gui_style_.detail_panel_width = g["detail_panel_width"].as<float>();
        if (g["row_height_scale"])
            gui_style_.row_height_scale   = g["row_height_scale"].as<float>();

        auto load_color = [&](const char *key, GuiStyle::Color4 &c) {
            if (g[key] && g[key].IsSequence() && g[key].size() == 4)
                for (std::size_t i = 0; i < 4; ++i)
                    c[i] = g[key][i].as<float>();
        };
        load_color("title_color",    gui_style_.title_color);
        load_color("bookmark_color", gui_style_.bookmark_color);
        load_color("accent_color",   gui_style_.accent_color);
        load_color("disabled_color", gui_style_.disabled_color);
    }
}

void Config::save_to_file(const std::string &path) const {
    YAML::Node config;

    YAML::Node article_settings;
    article_settings["download_dir"] = article_settings_.download_dir;
    article_settings["topics"]       = article_settings_.topics;
    config["article_settings"]       = article_settings;

    YAML::Node key_mappings;
    for (const auto &m : key_mappings_) {
        YAML::Node node;
        node["action"] = m.action;
        node["key"]    = m.key;
        key_mappings.push_back(node);
    }
    config["key_mappings"]      = key_mappings;
    config["recommend_threshold"] = recommend_threshold_;
    config["retrain_interval"]    = retrain_interval_;
    if (!obsidian_vault_.empty())
        config["obsidian_vault"]  = obsidian_vault_;
    if (auto_refresh_minutes_ > 0)
        config["auto_refresh_minutes"] = auto_refresh_minutes_;
    if (!keywords_file_.empty())
        config["keywords_file"]   = keywords_file_;
    if (ranker_file_ != "ranker.bin")
        config["ranker_file"]     = ranker_file_;

    // --- GUI style ---
    YAML::Node gui;
    gui["theme"]              = gui_style_.name;
    gui["font_size"]          = gui_style_.font_size;
    gui["filter_panel_width"] = gui_style_.filter_panel_width;
    gui["detail_panel_width"] = gui_style_.detail_panel_width;
    gui["row_height_scale"]   = gui_style_.row_height_scale;

    auto save_color = [&](const char *key, const GuiStyle::Color4 &c) {
        YAML::Node seq;
        for (float v : c) seq.push_back(v);
        gui[key] = seq;
    };
    save_color("title_color",    gui_style_.title_color);
    save_color("bookmark_color", gui_style_.bookmark_color);
    save_color("accent_color",   gui_style_.accent_color);
    save_color("disabled_color", gui_style_.disabled_color);
    config["gui"] = gui;

    std::ofstream fout(path);
    fout << config;
}

} // namespace Arxiv
