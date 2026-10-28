// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// A model file from disk must either load into a usable model or be
// rejected; it must never make prediction read or write out of bounds.

#include "Arxiv/Ranker.hh"

#include <chrono>
#include <cmath>

#include "FuzzSupport.hh"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    arxiv_fuzz::quiet_logs();
    const auto path = arxiv_fuzz::write_scratch_file(
        "ranker.bin", std::string_view(reinterpret_cast<const char*>(data), size));
    Arxiv::Ranker ranker;
    if (!ranker.Load(path))
        return 0;
    Arxiv::Article a("quark gluon plasma",
                     "https://arxiv.org/abs/2401.00001",
                     "lattice quark mass gluon",
                     "A. Author",
                     std::chrono::system_clock::now(),
                     "hep-ph");
    const float score = ranker.Predict(a);
    FUZZ_CHECK(std::isfinite(score));
    FUZZ_CHECK(std::isfinite(ranker.PredictBlended(a)));
    return 0;
}
