// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// Replays fuzz inputs without libFuzzer: every file argument (directories are
// walked) is passed once to LLVMFuzzerTestOneInput. Used by ctest so the
// corpus and past crashers run as regression tests in every build.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

static int run_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return 1;
}

int main(int argc, char** argv) {
    int ran = 0;
    for (int i = 1; i < argc; ++i) {
        std::filesystem::path p(argv[i]);
        if (std::filesystem::is_directory(p)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(p))
                if (entry.is_regular_file())
                    ran += run_file(entry.path());
        } else {
            ran += run_file(p);
        }
    }
    std::printf("replayed %d input(s)\n", ran);
    return 0;
}
