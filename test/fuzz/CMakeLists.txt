# SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
#
# SPDX-License-Identifier: GPL-3.0-only

# Fuzz targets. With ARXIV_TUI_ENABLE_FUZZING (Clang) each becomes a libFuzzer
# binary `fuzz_<name>`. In every testing build each also becomes
# `fuzz_<name>_replay`, which feeds the committed corpus to the target so
# seeds and past crashers run under ctest with any compiler.

set(ARXIV_FUZZ_TARGETS
    ranker_load
    feeds
    text
    config
    import_json
    replay
    digest_yaml
    db_ops
)

foreach(name ${ARXIV_FUZZ_TARGETS})
    set(corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name}")

    add_executable(fuzz_${name}_replay fuzz_${name}.cc StandaloneMain.cc)
    target_link_libraries(fuzz_${name}_replay PRIVATE arxiv_tui_options arxiv_tui_warnings libarxiv-tui)
    target_include_directories(fuzz_${name}_replay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(EXISTS "${corpus}")
        add_test(NAME fuzz_corpus_${name} COMMAND fuzz_${name}_replay "${corpus}")
    endif()

    if(ARXIV_TUI_ENABLE_FUZZING)
        add_executable(fuzz_${name} fuzz_${name}.cc)
        target_link_libraries(fuzz_${name} PRIVATE arxiv_tui_options arxiv_tui_warnings libarxiv-tui)
        target_include_directories(fuzz_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
    endif()
endforeach()
