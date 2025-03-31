#pragma once

#include <string>
#include <vector>
#include <optional>
#include <yaml-cpp/yaml.h>

namespace arxiv_tui {

class Config {
public:
    struct KeyMapping {
        std::string action;
        std::string key;
    };

    struct DownloadSettings {
        std::string download_dir;
        std::vector<std::string> topics;
        int max_papers_per_topic;
    };

    Config() = default;
    explicit Config(const std::string& config_file);

    // Getters
    const std::string& get_download_dir() const { return download_settings_.download_dir; }
    const std::vector<std::string>& get_topics() const { return download_settings_.topics; }
    int get_max_papers_per_topic() const { return download_settings_.max_papers_per_topic; }
    const std::vector<KeyMapping>& get_key_mappings() const { return key_mappings_; }

    // Setters
    void set_download_dir(const std::string& dir) { download_settings_.download_dir = dir; }
    void set_topics(const std::vector<std::string>& topics) { download_settings_.topics = topics; }
    void set_max_papers_per_topic(int max) { download_settings_.max_papers_per_topic = max; }
    void set_key_mappings(const std::vector<KeyMapping>& mappings) { key_mappings_ = mappings; }

    // Save/Load configuration
    void save_to_file(const std::string& config_file) const;
    void load_from_file(const std::string& config_file);

private:
    DownloadSettings download_settings_;
    std::vector<KeyMapping> key_mappings_;
};

} // namespace arxiv_tui 