# SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
#
# SPDX-License-Identifier: GPL-3.0-only

option(ENABLE_CPPCHECK "Enable static analysis with cppcheck" OFF)
option(ENABLE_CLANG_TIDY "Enable static analysis with clang-tidy" OFF)
if(ENABLE_CPPCHECK)
    find_program(CPPCHECK cppcheck)
    if(CPPCHECK)
        set(CMAKE_CXX_CPPCHECK ${CPPCHECK} --suppress=missingInclude --enable=all
                               --inconclusive)
    else()
        message(SEND_ERROR "cppcheck requested but executable not found")
    endif()
endif()

if(ENABLE_CLANG_TIDY)
    find_program(CLANGTIDY clang-tidy)
    if(CLANGTIDY)
        # Store the command but do NOT set CMAKE_CXX_CLANG_TIDY globally — that
        # would also run clang-tidy over every CPM dependency source file.
        # Individual targets opt in via set_target_properties instead.
        # --extra-arg silences GCC-only warning flags that end up in the
        # compile commands when the project is built with GCC.
        set(ARXIV_TUI_CLANG_TIDY_COMMAND
            "${CLANGTIDY};--extra-arg=-Wno-unknown-warning-option"
            CACHE INTERNAL "")
    else()
        message(SEND_ERROR "clang-tidy requested but executable not found")
    endif()
endif()
