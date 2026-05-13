#pragma once

#include "Arxiv/GuiStyle.hh"

#include <string>
#include <vector>
#include <optional>

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
    explicit Config(const std::string &config_file);

    // Getters
    const std::string& get_download_dir()         const { return article_settings_.download_dir; }
    const std::vector<std::string>& get_topics()  const { return article_settings_.topics; }
    const std::vector<KeyMapping>& get_key_mappings() const { return key_mappings_; }
    float get_recommend_threshold()               const { return recommend_threshold_; }
    int   get_retrain_interval()                  const { return retrain_interval_; }
    const std::string& get_keywords_file()        const { return keywords_file_; }
    const std::string& get_ranker_file()          const { return ranker_file_; }
    const std::string& get_obsidian_vault()       const { return obsidian_vault_; }
    int   get_auto_refresh_minutes()              const { return auto_refresh_minutes_; }
    const GuiStyle& get_gui_style()               const { return gui_style_; }
    // Path originally passed to the constructor; empty for default-constructed configs.
    const std::string& get_config_file()          const { return config_file_; }

    // Setters
    void set_download_dir(const std::string &dir)       { article_settings_.download_dir = dir; }
    void set_topics(const std::vector<std::string> &t)  { article_settings_.topics = t; }
    void set_key_mappings(const std::vector<KeyMapping> &m) { key_mappings_ = m; }
    void set_recommend_threshold(float t)               { recommend_threshold_ = t; }
    void set_retrain_interval(int n)                    { retrain_interval_ = n; }
    void set_keywords_file(const std::string &p)        { keywords_file_ = p; }
    void set_ranker_file(const std::string &p)          { ranker_file_ = p; }
    void set_obsidian_vault(const std::string &p)       { obsidian_vault_ = p; }
    void set_auto_refresh_minutes(int m)                { auto_refresh_minutes_ = m; }
    void set_gui_style(const GuiStyle &s)               { gui_style_ = s; }

    // Persist to disk.  save() reuses the path from the constructor;
    // save_to_file(path) writes to an explicit path.
    void save()                                   const { if (!config_file_.empty()) save_to_file(config_file_); }
    void save_to_file(const std::string &path)    const;
    void load_from_file(const std::string &path);

private:
    ArticleSettings      article_settings_;
    std::vector<KeyMapping> key_mappings_;
    float                recommend_threshold_{3.5f};
    int                  retrain_interval_{5};
    std::string          keywords_file_;
    std::string          ranker_file_{"ranker.bin"};
    std::string          obsidian_vault_;
    int                  auto_refresh_minutes_{0};
    GuiStyle             gui_style_;
    std::string          config_file_;   // path used for save()
};

} // namespace Arxiv
