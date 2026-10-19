"""Sphinx configuration for the geomon firmware documentation."""

import os

project = "geomon"
author = "jfcliche"
copyright = "2026, jfcliche"
release = "0.1.0"
version = release

extensions = [
    "myst_parser",
    "hawkmoth",
    "hawkmoth.ext.napoleon",
]

myst_enable_extensions = ["colon_fence"]
source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}

templates_path = ["_templates"]
exclude_patterns = ["_build", ".venv", "requirements.txt", "Thumbs.db", ".DS_Store"]

html_theme = "sphinx_rtd_theme"
html_theme_options = {
    "navigation_depth": 3,
    "collapse_navigation": False,
    "prev_next_buttons_location": "bottom",
}
html_static_path = ["_static"]
html_title = "geomon"

hawkmoth_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
hawkmoth_transform_default = "napoleon"
# The libclang wheel has no compiler and no C++ standard library. Skip the
# search for a clang binary and use the headers under docs/_clang.
hawkmoth_autoconf = []
hawkmoth_clang = [
    "-std=c++17",
    "-I" + os.path.join(os.path.dirname(__file__), "_clang"),
]
