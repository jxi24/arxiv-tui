// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"

#include <memory>
#include <vector>

#include "FuzzSupport.hh"

namespace arxiv_fuzz {

// Every file AppCore may write (exports, keywords, model) goes under the
// scratch directory, and nothing touches the network.
inline Arxiv::Config sandbox_config() {
    Arxiv::Config cfg;
    cfg.set_topics({"hep-ph", "hep-th"});
    cfg.set_download_dir((scratch_dir() / "downloads").string());
    cfg.set_obsidian_vault((scratch_dir() / "vault").string());
    cfg.set_keywords_file((scratch_dir() / "keywords.txt").string());
    cfg.set_ranker_file((scratch_dir() / "no-ranker.bin").string());
    return cfg;
}

inline std::unique_ptr<Arxiv::AppCore> make_core(const std::vector<Arxiv::Article>& articles = {}) {
    auto db = std::make_unique<Arxiv::DatabaseManager>(":memory:");
    for (const auto& a : articles)
        db->AddArticle(a);
    return std::make_unique<Arxiv::AppCore>(sandbox_config(),
                                            std::move(db),
                                            std::make_unique<OfflineFetcher>(),
                                            Arxiv::AppCore::FetchMode::Offline);
}

} // namespace arxiv_fuzz
