# SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
#
# SPDX-License-Identifier: GPL-3.0-only

# Coverage target using gcovr.
#
# Usage:
#   cmake -B build -DARXIV_TUI_ENABLE_TESTING=ON -DARXIV_TUI_COVERAGE=ON \
#         -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage -O0 -g"
#   cmake --build build
#   ctest --test-dir build
#   cmake --build build --target coverage          # text report
#   cmake --build build --target coverage-html     # HTML report in build/coverage/

find_program(GCOVR_EXECUTABLE gcovr)

if(NOT GCOVR_EXECUTABLE)
    message(STATUS "gcovr not found — coverage targets unavailable")
    return()
endif()

set(COVERAGE_MIN_LINE 85)

# Files excluded from measurement: platform/entry-point code and TUI rendering
# code that requires a live terminal and cannot be unit-tested in CI.
set(_COVERAGE_EXCLUDES
    "${CMAKE_SOURCE_DIR}/src/Arxiv/Views/.*"
    "${CMAKE_SOURCE_DIR}/src/Arxiv/main\\.cc"
    "${CMAKE_SOURCE_DIR}/src/Arxiv/Paths\\.cc"
    "${CMAKE_SOURCE_DIR}/src/Arxiv/CrashHandler\\.cc"
    "${CMAKE_SOURCE_DIR}/src/Arxiv/App\\.cc"
    "${CMAKE_SOURCE_DIR}/include/Arxiv/InstallPaths\\.hh"
)

# Build the --exclude flags list.
set(_GCOVR_EXCLUDE_FLAGS "")
foreach(_excl ${_COVERAGE_EXCLUDES})
    list(APPEND _GCOVR_EXCLUDE_FLAGS "--exclude" "${_excl}")
endforeach()

set(_GCOVR_COMMON_FLAGS
    --root "${CMAKE_SOURCE_DIR}"
    --filter "${CMAKE_SOURCE_DIR}/src/Arxiv/"
    --filter "${CMAKE_SOURCE_DIR}/include/Arxiv/"
    ${_GCOVR_EXCLUDE_FLAGS}
    --object-directory "${CMAKE_BINARY_DIR}"
)

# ── Text report (printed to stdout, fails build if below threshold) ──────────
add_custom_target(coverage
    COMMAND ${GCOVR_EXECUTABLE}
        ${_GCOVR_COMMON_FLAGS}
        --fail-under-line ${COVERAGE_MIN_LINE}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Generating line-coverage report (threshold: ${COVERAGE_MIN_LINE}%)"
    VERBATIM
)

# ── HTML report (written to build/coverage/index.html) ───────────────────────
add_custom_target(coverage-html
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/coverage"
    COMMAND ${GCOVR_EXECUTABLE}
        ${_GCOVR_COMMON_FLAGS}
        --html-details "${CMAKE_BINARY_DIR}/coverage/index.html"
        --fail-under-line ${COVERAGE_MIN_LINE}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Generating HTML coverage report in ${CMAKE_BINARY_DIR}/coverage/"
    VERBATIM
)

# ── Cobertura XML (consumed by GitHub Actions / other CI tools) ───────────────
add_custom_target(coverage-xml
    COMMAND ${GCOVR_EXECUTABLE}
        ${_GCOVR_COMMON_FLAGS}
        --cobertura "${CMAKE_BINARY_DIR}/coverage.xml"
        --fail-under-line ${COVERAGE_MIN_LINE}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Generating Cobertura XML coverage report"
    VERBATIM
)
