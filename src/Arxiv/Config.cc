#include "Arxiv/Config.hh"
#include <filesystem>
#include <fstream>

namespace Arxiv {

Config::Config(const std::string& config_file) {
    load_from_file(config_file);
}

void Config::load_from_file(const std::string& config_file) {
    if (!std::filesystem::exists(config_file)) {
        // Create default configuration
        article_settings_.download_dir = std::filesystem::current_path() / "articles";
        article_settings_.topics = {"hep-ph", "hep-ex", "hep-lat", "hep-th"};
        
        // Default key mappings
        key_mappings_ = {
            {"next", "j"},
            {"previous", "k"},
            {"quit", "q"},
            {"create_project", "p"},
            {"delete_project", "x"},
            {"download_article", "d"},  
            {"show_detail", "a"},
        };
        
        save_to_file(config_file);
        return;
    }

    YAML::Node config = YAML::LoadFile(config_file);

    // Load download settings
    if (config["article_settings"]) {
        const auto& ds = config["article_settings"];
        article_settings_.download_dir = ds["download_dir"].as<std::string>();
        article_settings_.topics = ds["topics"].as<std::vector<std::string>>();
    }

    // Load key mappings
    if (config["key_mappings"]) {
        key_mappings_.clear();
        for (const auto& mapping : config["key_mappings"]) {
            KeyMapping km;
            km.action = mapping["action"].as<std::string>();
            km.key = mapping["key"].as<std::string>();
            key_mappings_.push_back(km);
        }
    }
}

void Config::save_to_file(const std::string& config_file) const {
    YAML::Node config;

    // Save download settings
    YAML::Node article_settings;
    article_settings["download_dir"] = article_settings_.download_dir;
    article_settings["topics"] = article_settings_.topics;
    config["article_settings"] = article_settings;

    // Save key mappings
    YAML::Node key_mappings;
    for (const auto& mapping : key_mappings_) {
        YAML::Node mapping_node;
        mapping_node["action"] = mapping.action;
        mapping_node["key"] = mapping.key;
        key_mappings.push_back(mapping_node);
    }
    config["key_mappings"] = key_mappings;

    std::ofstream fout(config_file);
    fout << config;
}

} // namespace arxiv_tui 
