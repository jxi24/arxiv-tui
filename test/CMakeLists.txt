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
)

# Link against Catch2 and our library
target_link_libraries(unit_tests PRIVATE
    Catch2::Catch2WithMain
    trompeloeil
    arxiv_tui_options
    arxiv_tui_warnings
    libarxiv-tui
)

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

# Register tests
include(CTest)
catch_discover_tests(unit_tests)
catch_discover_tests(integration_tests)
