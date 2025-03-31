#pragma once

#include <string>
#include <vector>
#include <optional>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast" 
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

    // Setters
    void set_download_dir(const std::string& dir) { article_settings_.download_dir = dir; }
    void set_topics(const std::vector<std::string>& topics) { article_settings_.topics = topics; }
    void set_key_mappings(const std::vector<KeyMapping>& mappings) { key_mappings_ = mappings; }

    // Save/Load configuration
    void save_to_file(const std::string& config_file) const;
    void load_from_file(const std::string& config_file);

private:
    ArticleSettings article_settings_;
    std::vector<KeyMapping> key_mappings_;
};

} // namespace arxiv_tui 
