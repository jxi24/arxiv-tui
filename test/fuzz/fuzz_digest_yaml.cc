// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// The YAML digest must parse back to exactly the article text that went in,
// whatever characters (LaTeX, quotes, newlines, control bytes) it contains.

#include "CoreHarness.hh" // brings in yaml-cpp via Config.hh

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    arxiv_fuzz::quiet_logs();
    arxiv_fuzz::Bytes in(data, size);
    Arxiv::Article a;
    a.title = in.string(200);
    a.authors = in.string(120);
    a.category = in.string(40);
    a.abstract = in.rest();
    // Feeds are decoded UTF-8; YAML cannot carry arbitrary bytes.
    if (!arxiv_fuzz::valid_utf8(a.title) || !arxiv_fuzz::valid_utf8(a.authors) ||
        !arxiv_fuzz::valid_utf8(a.category) || !arxiv_fuzz::valid_utf8(a.abstract))
        return 0;
    a.link = "https://arxiv.org/abs/2401.00001";
    a.date = std::chrono::system_clock::now();

    auto core = arxiv_fuzz::make_core({a});
    const auto out = (arxiv_fuzz::scratch_dir() / "digest.yaml").string();
    FUZZ_CHECK(core->ExportDailyDigestYAML(out));

    YAML::Node doc;
    try {
        doc = YAML::LoadFile(out);
    } catch (const YAML::Exception&) {
        arxiv_fuzz::fail("digest is not valid YAML");
    }
    const YAML::Node articles = doc["articles"];
    FUZZ_CHECK(articles.IsSequence() && articles.size() == 1);
    const YAML::Node entry = *articles.begin();
    FUZZ_CHECK(entry["title"].as<std::string>() == a.title);
    FUZZ_CHECK(entry["authors"].as<std::string>() == a.authors);
    FUZZ_CHECK(entry["category"].as<std::string>() == a.category);
    if (!a.abstract.empty())
        FUZZ_CHECK(entry["abstract"].as<std::string>() == a.abstract);
    return 0;
}
