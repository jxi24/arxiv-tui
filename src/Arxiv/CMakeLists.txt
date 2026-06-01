# SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
#
# SPDX-License-Identifier: GPL-3.0-only

add_library(libarxiv-tui
    Paths.cc
    App.cc
    AppCore.cc
    Clipboard.cc
    Fetcher.cc
    DatabaseManager.cc
    Config.cc
    KeyBindings.cc
    LatexUtils.cc
    Ranker.cc
    Replay.cc
    CrashHandler.cc
    Views/FilterPane.cc
    Views/ArticlePane.cc
    Views/DetailView.cc
    Views/HelpDialog.cc
    Views/AssignProjectDialog.cc
    Views/DateRangeDialog.cc
    Views/SearchDialog.cc
    Views/RatingDialog.cc
    Views/NotesDialog.cc
    Views/ExportDialog.cc
    Views/ImportDialog.cc
    Views/KeywordEditorDialog.cc
    Views/CategoryFilterDialog.cc
    Views/SettingsDialog.cc
    Views/NewProjectDialog.cc
    Views/MainRenderer.cc
)
target_link_libraries(libarxiv-tui PRIVATE arxiv_tui_options arxiv_tui_warnings
                                    PUBLIC spdlog::spdlog std::filesystem
                                           cpr::cpr ftxui::screen ftxui::dom
                                           ftxui::component SQLite::SQLite3 pugixml::pugixml
                                           yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json)

add_executable(arxiv-tui
    main.cc
)
target_link_libraries(arxiv-tui PUBLIC arxiv_tui_options arxiv_tui_warnings libarxiv-tui)

if(ARXIV_TUI_CLANG_TIDY_COMMAND)
    set_target_properties(libarxiv-tui arxiv-tui PROPERTIES
        CXX_CLANG_TIDY "${ARXIV_TUI_CLANG_TIDY_COMMAND}")
endif()

# ── Install rules ────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS arxiv-tui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS libarxiv-tui
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/Arxiv
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hh"
    PATTERN "*.hh.in" EXCLUDE
)

# Install the generated InstallPaths.hh alongside the source headers.
install(FILES ${CMAKE_BINARY_DIR}/include/Arxiv/InstallPaths.hh
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Arxiv
)
