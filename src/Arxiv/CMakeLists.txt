add_library(libarxiv-tui
    App.cc
    AppCore.cc
    Fetcher.cc
    DatabaseManager.cc
)
target_link_libraries(libarxiv-tui PRIVATE arxiv_tui_options arxiv_tui_warnings
                                    PUBLIC spdlog::spdlog std::filesystem
                                           cpr::cpr ftxui::screen ftxui::dom
                                           ftxui::component SQLite::SQLite3 pugixml::pugixml)

add_executable(arxiv-tui
    main.cc
)
target_link_libraries(arxiv-tui PUBLIC arxiv_tui_options arxiv_tui_warnings libarxiv-tui)
