# SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
#
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.17)

# Find Catch2
CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    VERSION 3.5.0
    OPTIONS
        "BUILD_TESTING OFF"
        "CATCH_INSTALL_DOCS OFF"
        "CATCH_INSTALL_EXTRAS OFF"
)

# Add trompeloeil
CPMAddPackage(
    NAME trompeloeil
    GITHUB_REPOSITORY rollbear/trompeloeil
    VERSION 47
)

# Create test executable
add_executable(unit_tests
    unit/DatabaseManagerTest.cc
    unit/DatabaseManagerRealTest.cc
    unit/DatabaseManagerIntegrationTest.cc
    unit/FetcherTest.cc
    unit/FetcherRealTest.cc
    unit/AppTest.cc
    unit/RankerTest.cc
    unit/ReplayTest.cc
    unit/BibTeXTest.cc
    unit/LatexTest.cc
    unit/KeywordTest.cc
    unit/KeywordEditorTest.cc
    unit/DigestTest.cc
    unit/AuthorSubscriptionTest.cc
    unit/FuzzySearchTest.cc
    unit/BackgroundRefreshTest.cc
    unit/NewArticlesTest.cc
    unit/KeyBindingsTest.cc
    unit/ConfigTest.cc
    unit/AppTuiTest.cc
)

# Link against Catch2 and our library
target_link_libraries(unit_tests PRIVATE
    Catch2::Catch2WithMain
    trompeloeil
    arxiv_tui_options
    arxiv_tui_warnings
    libarxiv-tui
)

# Use DT_RPATH instead of DT_RUNPATH so the build-tree libraries take precedence
# over LD_LIBRARY_PATH. This is required for catch_discover_tests, which runs the
# binary during the build before any ctest ENVIRONMENT properties take effect.
target_link_options(unit_tests PRIVATE -Wl,--disable-new-dtags)

# Add include directories for test files
target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/test
    ${CMAKE_SOURCE_DIR}/test/mocks
    ${CMAKE_SOURCE_DIR}/test/fixtures
)

include(FetchContent)
FetchContent_GetProperties(catch2)
include(${catch2_SOURCE_DIR}/extras/Catch.cmake)

# --- Integration / stress tests ---
add_executable(integration_tests
    integration/StressTest.cc
)

target_link_libraries(integration_tests PRIVATE
    Catch2::Catch2WithMain
    trompeloeil
    arxiv_tui_options
    arxiv_tui_warnings
    libarxiv-tui
)
target_link_options(integration_tests PRIVATE -Wl,--disable-new-dtags)

target_include_directories(integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/test
    ${CMAKE_SOURCE_DIR}/test/mocks
    ${CMAKE_SOURCE_DIR}/test/fixtures
)

# Workaround: GCC 13+ emits false-positive -Wfree-nonheap-object diagnostics
# when destructing aggregates containing std::vector<std::string> that are
# returned by value and consumed via structured bindings (as make_core() is in
# StressTest.cc). The generated code is correct; suppress the diagnostic so
# -Werror builds succeed.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(integration_tests PRIVATE -Wno-free-nonheap-object)
endif()

if(ARXIV_TUI_CLANG_TIDY_COMMAND)
    set_target_properties(unit_tests integration_tests PROPERTIES
        CXX_CLANG_TIDY "${ARXIV_TUI_CLANG_TIDY_COMMAND}")
endif()

# Register tests
include(CTest)
catch_discover_tests(unit_tests)
catch_discover_tests(integration_tests)
