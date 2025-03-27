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
    unit/FetcherTest.cc
    unit/AppTest.cc
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

# Register tests
include(CTest)
catch_discover_tests(unit_tests) 
