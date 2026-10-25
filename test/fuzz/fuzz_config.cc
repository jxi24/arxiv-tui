// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// config.yml is hand-edited: any bytes either load (bad values falling back
// to defaults) or fail with a YAML parse error, and what loads survives a
// save/load round trip.

#include "Arxiv/Config.hh"

#include <optional>

#include "FuzzSupport.hh"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    arxiv_fuzz::quiet_logs();
    const auto path = arxiv_fuzz::write_scratch_file(
        "config.yml", std::string_view(reinterpret_cast<const char*>(data), size));
    std::optional<Arxiv::Config> cfg;
    try {
        cfg.emplace(path);
    } catch (const YAML::Exception&) {
        return 0; // not YAML at all: reported to the user by main
    }
    FUZZ_CHECK(cfg->get_retrain_interval() > 0);
    FUZZ_CHECK(cfg->get_undo_buffer_size() <= 1000);
    FUZZ_CHECK(cfg->get_scroll_margin() >= 0);

    // YAML is Unicode: a value that is not valid UTF-8 cannot survive a
    // save/load cycle byte for byte, so only check the round trip otherwise.
    for (const auto& str : {cfg->get_download_dir(), cfg->get_obsidian_vault()})
        if (!arxiv_fuzz::valid_utf8(str))
            return 0;
    for (const auto& t : cfg->get_topics())
        if (!arxiv_fuzz::valid_utf8(t))
            return 0;

    const auto saved = (arxiv_fuzz::scratch_dir() / "config_roundtrip.yml").string();
    cfg->save_to_file(saved);
    Arxiv::Config again(saved);
    FUZZ_CHECK(again.get_topics() == cfg->get_topics());
    FUZZ_CHECK(again.get_download_dir() == cfg->get_download_dir());
    FUZZ_CHECK(again.get_retrain_interval() == cfg->get_retrain_interval());
    FUZZ_CHECK(again.get_undo_buffer_size() == cfg->get_undo_buffer_size());
    FUZZ_CHECK(again.get_obsidian_vault() == cfg->get_obsidian_vault());
    FUZZ_CHECK(again.get_key_mappings().size() == cfg->get_key_mappings().size());
    return 0;
}
