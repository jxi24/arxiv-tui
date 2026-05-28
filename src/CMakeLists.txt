# SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
#
# SPDX-License-Identifier: GPL-3.0-only

add_subdirectory(Arxiv)
if(ARXIV_TUI_BUILD_GUI)
    add_subdirectory(GUI)
endif()
