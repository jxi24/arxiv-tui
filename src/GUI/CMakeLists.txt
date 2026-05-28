# SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
#
# SPDX-License-Identifier: GPL-3.0-only

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

# arxiv-gui executable
add_executable(arxiv-gui main.cc)
target_link_libraries(arxiv-gui
    PRIVATE
        arxiv_tui_options
        arxiv_tui_warnings
        libarxiv-tui
        ftxui::component
        ftxui::dom
        ftxui::screen
        imgui
        glfw
        OpenGL::GL
)
