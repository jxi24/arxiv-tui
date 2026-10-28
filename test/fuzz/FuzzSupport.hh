// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Shared helpers for the fuzz targets. Kept free of libFuzzer headers so the
// same sources also build with GCC into the corpus-replay binaries.

#pragma once

#include "Arxiv/Article.hh"
#include "Arxiv/Fetcher.hh"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "spdlog/spdlog.h"

namespace arxiv_fuzz {

// Abort with a message: libFuzzer (and the replay runner) treat it as a crash.
[[noreturn]] inline void fail(const char* what) {
    std::fprintf(stderr, "fuzz invariant violated: %s\n", what);
    std::abort();
}
#define FUZZ_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond))                                                                               \
            ::arxiv_fuzz::fail(#cond);                                                             \
    } while (0)

// Silence the application's logging once per process (it is noise and slow).
inline void quiet_logs() {
    static bool done = [] {
        spdlog::set_level(spdlog::level::off);
        return true;
    }();
    (void)done;
}

// A per-process scratch directory, removed and recreated on demand.
inline std::filesystem::path scratch_dir() {
    static const std::filesystem::path dir = [] {
        auto d =
            std::filesystem::temp_directory_path() / ("arxiv_fuzz_" + std::to_string(::getpid()));
        std::filesystem::create_directories(d);
        return d;
    }();
    return dir;
}

inline std::string write_scratch_file(const std::string& name, std::string_view bytes) {
    auto path = (scratch_dir() / name).string();
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        .write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
}

// Minimal stand-in for FuzzedDataProvider (a Clang-only header).
class Bytes {
  public:
    Bytes(const std::uint8_t* data, std::size_t size)
        : m_data(data)
        , m_size(size) {}

    bool empty() const { return m_pos >= m_size; }

    std::uint8_t byte() { return empty() ? 0 : m_data[m_pos++]; }

    // A string of up to max_len bytes, length taken from the next byte.
    std::string string(std::size_t max_len) {
        std::size_t len = std::min<std::size_t>(byte(), max_len);
        len = std::min(len, m_size - std::min(m_pos, m_size));
        std::string s(reinterpret_cast<const char*>(m_data + m_pos), len);
        m_pos += len;
        return s;
    }

    std::string rest() {
        if (empty())
            return {};
        std::string s(reinterpret_cast<const char*>(m_data + m_pos), m_size - m_pos);
        m_pos = m_size;
        return s;
    }

  private:
    const std::uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_pos = 0;
};

// A fetcher that never touches the network.
class OfflineFetcher : public Arxiv::Fetcher {
  public:
    OfflineFetcher()
        : Arxiv::Fetcher({"hep-ph"}, (scratch_dir() / "downloads").string()) {}
    std::vector<Arxiv::Article> Fetch() override { return {}; }
    std::vector<Arxiv::Article> FetchSince(const std::string&) override { return {}; }
    bool DownloadPaper(const std::string&, const std::string&) override { return false; }
    std::string FetchBibTeX(const std::string&) override { return {}; }
};

// Strict UTF-8 (RFC 3629): no overlong forms, surrogates or code points
// above U+10FFFF.
inline bool valid_utf8(std::string_view s) {
    auto byte = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = byte(i);
        std::size_t extra = 0;
        unsigned char lo = 0x80, hi = 0xBF; // allowed range of the 2nd byte
        if (c < 0x80) {
            ++i;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            extra = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            extra = 2;
            if (c == 0xE0)
                lo = 0xA0;
            if (c == 0xED)
                hi = 0x9F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            extra = 3;
            if (c == 0xF0)
                lo = 0x90;
            if (c == 0xF4)
                hi = 0x8F;
        } else {
            return false;
        }
        if (i + extra >= s.size())
            return false;
        if (byte(i + 1) < lo || byte(i + 1) > hi)
            return false;
        for (std::size_t k = 2; k <= extra; ++k)
            if ((byte(i + k) >> 6) != 0x2)
                return false;
        i += extra + 1;
    }
    return true;
}

} // namespace arxiv_fuzz
