// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <optional>
#include <string>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <yaml-cpp/yaml.h>
#pragma GCC diagnostic pop

namespace Arxiv {

class Config {
  public:
    struct KeyMapping {
        std::string action;
        std::string key;
    };

    struct ArticleSettings {
        std::string download_dir;
        std::vector<std::string> topics;
    };

    Config() = default;
    explicit Config(const std::string& config_file);

    // Getters
    const std::string& get_download_dir() const { return article_settings_.download_dir; }
    const std::vector<std::string>& get_topics() const { return article_settings_.topics; }
    const std::vector<KeyMapping>& get_key_mappings() const { return key_mappings_; }
    float get_recommend_threshold() const { return recommend_threshold_; }
    int get_retrain_interval() const { return retrain_interval_; }
    const std::string& get_db_file() const { return db_file_; }
    const std::string& get_keywords_file() const { return keywords_file_; }
    const std::string& get_ranker_file() const { return ranker_file_; }
    const std::string& get_obsidian_vault() const { return obsidian_vault_; }
    int get_auto_refresh_minutes() const { return auto_refresh_minutes_; }
    int get_scroll_margin() const { return scroll_margin_; }
    int get_max_article_age_days() const { return max_article_age_days_; }

    // Setters
    void set_download_dir(const std::string& dir) { article_settings_.download_dir = dir; }
    void set_topics(const std::vector<std::string>& topics) { article_settings_.topics = topics; }
    void set_key_mappings(const std::vector<KeyMapping>& mappings) { key_mappings_ = mappings; }
    void set_recommend_threshold(float t) { recommend_threshold_ = t; }
    void set_retrain_interval(int n) { retrain_interval_ = n; }
    void set_db_file(const std::string& path) { db_file_ = path; }
    void set_keywords_file(const std::string& path) { keywords_file_ = path; }
    void set_ranker_file(const std::string& path) { ranker_file_ = path; }
    void set_obsidian_vault(const std::string& path) { obsidian_vault_ = path; }
    void set_auto_refresh_minutes(int m) { auto_refresh_minutes_ = m; }
    void set_scroll_margin(int n) { scroll_margin_ = n; }
    void set_max_article_age_days(int n) { max_article_age_days_ = n; }

    // Save/Load configuration
    void save_to_file(const std::string& config_file) const;
    void load_from_file(const std::string& config_file);

  private:
    ArticleSettings article_settings_;
    std::vector<KeyMapping> key_mappings_;
    float recommend_threshold_{3.5f};
    int retrain_interval_{5};
    std::string db_file_{"articles.db"};
    std::string keywords_file_;
    std::string ranker_file_{"ranker.bin"};
    std::string obsidian_vault_;
    int auto_refresh_minutes_{0};
    int scroll_margin_{3};
    int max_article_age_days_{0};
};

} // namespace Arxiv
