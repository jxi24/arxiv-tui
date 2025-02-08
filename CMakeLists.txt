cmake_minimum_required(VERSION 3.17)

# Policy to address @foo@ variable expansion
if(POLICY CMP0053)
  cmake_policy(SET CMP0053 NEW)
endif()

# Set the project name and basic settings
project(ARXIV_TUI LANGUAGES CXX VERSION 1.0.0)
include(CMake/StandardProjectSettings.cmake)

# RPATH handling
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Set RPATH for the install location
LIST(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" isSystemDir)
IF("${isSystemDir}" STREQUAL "-1")
  if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
    list(APPEND CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
  elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib;${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
  endif()
ENDIF()

# Link this 'library' to set the c++ standard / compile-time options requested
# Additionally, link to get include and external dependencies
add_library(arxiv_tui_options INTERFACE)
target_compile_features(arxiv_tui_options INTERFACE cxx_std_17)
target_include_directories(arxiv_tui_options INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
add_library(arxiv_tui_warnings INTERFACE)

# Standard compiler warnings
include(CMake/CompilerWarnings.cmake)
set_project_warnings(arxiv_tui_warnings)

# Sanitizer options if supported by compiler
include(CMake/Sanitizers.cmake)
enable_sanitizers(arxiv_tui_options)

# Allow for static analysis options
include(CMake/StaticAnalyzers.cmake)
include(CMake/ProgramOptions.cmake)

# Allow build to be made even if not a git repo
SET(GIT_FAIL_IF_NONZERO_EXIT FALSE)

# Add CMake modules to CMAKE_MODULE_PATH
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/CMake)

# Ensure compiler supports filesystem and needed compiler flags
find_package(Filesystem REQUIRED)

# Add dependencies
include(CMake/CPM.cmake)
include(Dependencies.txt)

find_package(SQLite3 REQUIRED)

# Testing
if(ARXIV_TUI_ENABLE_TESTING)
    if(ARXIV_TUI_COVERAGE)
    target_compile_options(arxiv_tui_options INTERFACE --coverage)
    target_link_libraries(arxiv_tui_options INTERFACE gcov)
  endif()
  target_compile_definitions(arxiv_tui_options INTERFACE TESTING)
  enable_testing()
  message(STATUS "Building Tests.")
  add_subdirectory(test)
endif()

# Main code
add_subdirectory(src)
