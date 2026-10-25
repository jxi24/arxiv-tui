// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Project JSON from disk is untrusted: import must not throw, and only
// canonical arXiv links may end up in the project.

#include "CoreHarness.hh"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    arxiv_fuzz::quiet_logs();
    auto core = arxiv_fuzz::make_core();
    const auto path = arxiv_fuzz::write_scratch_file(
        "import.json", std::string_view(reinterpret_cast<const char*>(data), size));
    bool ok = false;
    try {
        ok = core->ImportProjectJSON(path);
    } catch (...) {
        arxiv_fuzz::fail("ImportProjectJSON threw");
    }
    if (!ok)
        return 0;
    for (const auto& project : core->GetProjects())
        for (const auto& a : core->GetArticlesForProject(project))
            FUZZ_CHECK(Arxiv::Fetcher::IsArxivAbsLink(a.link));
    return 0;
}
