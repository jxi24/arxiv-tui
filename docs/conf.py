# SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
#
# SPDX-License-Identifier: GPL-3.0-only

project = "arxiv-tui"
copyright = "2024-2026 Josh Isaacson"
author = "Josh Isaacson"

extensions = [
    "sphinx_copybutton",
]

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

html_theme = "furo"
html_title = "arxiv-tui"
html_logo = "_static/logo.svg"
html_favicon = "_static/logo.svg"
html_static_path = ["_static"]

html_theme_options = {
    "sidebar_hide_name": False,
    "navigation_with_keys": True,
}

html_sidebars = {
    "**": [
        "sidebar/brand.html",
        "sidebar/search.html",
        "sidebar/scroll-start.html",
        "sidebar/navigation.html",
        "versioning.html",
        "sidebar/scroll-end.html",
    ]
}

# Populated by the CI workflow via -D html_context.versions and
# html_context.current_version so the versioning sidebar works correctly.
# Locally these stay empty and the sidebar is simply hidden.
html_context = {
    "versions": [],       # list of {"name": str, "url": str}
    "current_version": "",
}
