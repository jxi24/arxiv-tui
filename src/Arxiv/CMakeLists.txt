# Core business logic — no TUI/FTXUI UI (App.cc / Components.cc).
# Linked by tests and the ImGui GUI frontend to avoid pulling in the FTXUI
# static initializers that trigger a nullptr-deref in g_quirks when loaded
# outside a proper FTXUI event loop.
add_library(libarxiv-core
    AppCore.cc
    Fetcher.cc
    DatabaseManager.cc
    Config.cc
    KeyBindings.cc
    LatexUtils.cc
    Ranker.cc
    Replay.cc
    CrashHandler.cc
)
target_link_libraries(libarxiv-core
    PRIVATE arxiv_tui_options arxiv_tui_warnings
    PUBLIC  spdlog::spdlog std::filesystem
            cpr::cpr ftxui::screen ftxui::dom
            ftxui::component SQLite::SQLite3 pugixml::pugixml
            yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json)

# Full TUI library — adds the FTXUI shell (App.cc).
# Only linked by the arxiv-tui terminal executable.
add_library(libarxiv-tui
    App.cc
)
target_link_libraries(libarxiv-tui
    PRIVATE arxiv_tui_options arxiv_tui_warnings
    PUBLIC  libarxiv-core)

add_executable(arxiv-tui
    main.cc
)
target_link_libraries(arxiv-tui PUBLIC arxiv_tui_options arxiv_tui_warnings libarxiv-tui)
