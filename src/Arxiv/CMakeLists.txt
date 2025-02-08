add_executable(arxiv-tui
    main.cc
    App.cc
    Fetcher.cc
    Components.cc
    DatabaseManager.cc
)
target_link_libraries(arxiv-tui PUBLIC arxiv_tui_options arxiv_tui_warnings
                                        spdlog::spdlog std::filesystem
                                        cpr::cpr ftxui::screen ftxui::dom
                                        ftxui::component SQLite::SQLite3 pugixml::pugixml)
