# SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
#
# SPDX-License-Identifier: GPL-3.0-only

import json
import os

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

# Populated by the CI workflow via SPHINX_VERSIONS (JSON array) and
# SPHINX_CURRENT_VERSION env vars. Locally these stay empty and the sidebar
# is simply hidden.
html_context = {
    "versions": json.loads(os.environ.get("SPHINX_VERSIONS", "[]")),
    "current_version": os.environ.get("SPHINX_CURRENT_VERSION", ""),
}
