// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// A replay log is untrusted input. Lines must be applied or reported, never
// crash, and AppCore must stay consistent (index in range). File paths in
// the log are redirected into the scratch directory before replaying.

#include "Arxiv/Replay.hh"

#include <nlohmann/json.hpp>

#include <sstream>

#include "CoreHarness.hh"

namespace {

std::vector<Arxiv::Article> seed_articles() {
    std::vector<Arxiv::Article> v;
    const char* cats[] = {"hep-ph", "hep-th", "hep-ph, hep-th", ""};
    for (int i = 0; i < 4; ++i)
        v.emplace_back("Title " + std::to_string(i),
                       "https://arxiv.org/abs/2401.0000" + std::to_string(i),
                       "Abstract",
                       "A. Author, B. Author",
                       std::chrono::system_clock::now(),
                       cats[i]);
    return v;
}

// Keep the replay from writing outside the sandbox.
std::string sandbox_paths(const std::string& jsonl) {
    std::istringstream in(jsonl);
    std::string line, out;
    while (std::getline(in, line)) {
        try {
            auto j = nlohmann::json::parse(line);
            if (j.is_object() && j.contains("path"))
                j["path"] = (arxiv_fuzz::scratch_dir() / "replay_out").string();
            line = j.dump();
        } catch (const nlohmann::json::exception&) {
            // leave malformed lines as they are: the player must reject them
        }
        out += line;
        out += '\n';
    }
    return out;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    arxiv_fuzz::quiet_logs();
    auto core = arxiv_fuzz::make_core(seed_articles());
    const std::string jsonl = sandbox_paths(std::string(reinterpret_cast<const char*>(data), size));
    try {
        Arxiv::ReplayPlayer::FromString(jsonl, *core);
    } catch (...) {
        arxiv_fuzz::fail("ReplayPlayer::FromString threw");
    }
    const auto n = static_cast<int>(core->GetCurrentArticles().size());
    FUZZ_CHECK(core->GetArticleIndex() >= 0);
    FUZZ_CHECK(n == 0 || core->GetArticleIndex() < n);
    FUZZ_CHECK(core->GetCurrentTitles().size() == core->GetCurrentArticles().size());
    core->WaitForBackgroundTask();
    return 0;
}
