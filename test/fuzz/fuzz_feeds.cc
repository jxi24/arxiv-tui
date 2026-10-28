// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// RSS and Atom parsing of untrusted feed bytes: no crash, and every article
// that comes out carries a canonical arXiv abstract link (links reach file
// names and xdg-open).

#include "FuzzSupport.hh"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    arxiv_fuzz::quiet_logs();
    static arxiv_fuzz::OfflineFetcher fetcher;
    if (size == 0)
        return 0;
    // First byte picks the parser so one corpus covers both.
    const std::string xml(reinterpret_cast<const char*>(data) + 1, size - 1);
    const auto articles = (data[0] & 1) ? fetcher.ParseAtomFeed(xml) : fetcher.ParseFeed(xml);
    for (const auto& a : articles) {
        FUZZ_CHECK(Arxiv::Fetcher::IsArxivLink(a.link));
        FUZZ_CHECK(a.file_stem().find('/') == std::string::npos);
    }
    return 0;
}
