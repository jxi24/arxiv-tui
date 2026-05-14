find_package(OpenGL REQUIRED)

# Build ImGui as a static library from CPM-fetched sources.
# ImGui provides no CMake of its own, so we define the target here.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# imgui-headless: ImGui without any backend (GLFW/OpenGL).
# Used by tests that drive frames without a display.
add_library(imgui_headless STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui_headless SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# ArxivGuiApp logic — no main(), no GLFW/OpenGL.
# Linked by both the arxiv-gui binary and the gui_tests executable.
add_library(arxiv-gui-lib STATIC
    ArxivGuiApp.cc
    panels/FilterPanel.cc
    panels/ArticlePanel.cc
    panels/DetailPanel.cc
    panels/SettingsPanel.cc
)
target_include_directories(arxiv-gui-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arxiv-gui-lib
    PUBLIC
        libarxiv-core
        imgui_headless
    PRIVATE
        arxiv_tui_options
        arxiv_tui_warnings
)

# arxiv-gui executable — adds the GLFW/OpenGL backends + main.
add_executable(arxiv-gui main.cc)
target_link_libraries(arxiv-gui
    PRIVATE
        arxiv_tui_options
        arxiv_tui_warnings
        arxiv-gui-lib
        imgui          # full imgui with GLFW/OpenGL backends
        glfw
        OpenGL::GL
)
