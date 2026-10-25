// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Text helpers fed by feed content: LaTeX conversion, link normalisation,
// date parsing and fuzzy matching. Checks bounds and idempotence.

#include "Arxiv/FuzzyMatch.hh"
#include "Arxiv/LatexUtils.hh"

#include "FuzzSupport.hh"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    arxiv_fuzz::quiet_logs();
    static arxiv_fuzz::OfflineFetcher fetcher;
    arxiv_fuzz::Bytes in(data, size);
    const std::uint8_t which = in.byte();
    switch (which % 4) {
    case 0: {
        const std::string text = in.rest();
        const std::string md = fetcher.LatexToMarkdown(text);
        // Conversions only replace commands with shorter markers or accents.
        FUZZ_CHECK(md.size() <= text.size() * 4 + 16);
        FUZZ_CHECK(Arxiv::StripLatex(text).size() <= text.size() * 4 + 16);
        break;
    }
    case 1: {
        const std::string link = in.rest();
        const std::string once = Arxiv::Fetcher::NormalizeLink(link);
        FUZZ_CHECK(Arxiv::Fetcher::NormalizeLink(once) == once);
        if (Arxiv::Fetcher::IsArxivLink(once)) {
            Arxiv::Article a;
            a.link = once;
            FUZZ_CHECK(!a.id().empty());
            FUZZ_CHECK(a.file_stem().find('/') == std::string::npos);
        }
        break;
    }
    case 2: {
        const std::string date = in.rest();
        (void)fetcher.ParseDate(date);
        (void)fetcher.ParseAtomDate(date);
        (void)fetcher.FetchSinceWindowStart(date);
        break;
    }
    case 3: {
        // Edit distance is quadratic; keep inputs small.
        const std::string a = in.string(64);
        const std::string b = in.string(256);
        const int sim = Arxiv::FuzzyMatch::Similarity(a, b);
        FUZZ_CHECK(sim >= 0 && sim <= 100);
        FUZZ_CHECK(sim == Arxiv::FuzzyMatch::Similarity(b, a));
        FUZZ_CHECK(Arxiv::FuzzyMatch::Similarity(a, a) == 100);
        (void)Arxiv::FuzzyMatch::MatchesText(a, b, 80);
        break;
    }
    }
    return 0;
}
