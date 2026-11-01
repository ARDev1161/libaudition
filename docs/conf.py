"""Sphinx website for libaudition.

Build Doxygen XML first: doxygen docs/Doxyfile.pages
Then run: sphinx-build -b html docs docs/_build/html
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
project = "libaudition"
author = "libaudition contributors"

cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
version_match = re.search(r"project\(libaudition VERSION ([0-9.]+)", cmake_text)
version = version_match.group(1) if version_match else "dev"
release = version

extensions = [
    "myst_parser",
    "sphinxcontrib.mermaid",
    "breathe",
]

source_suffix = {".rst": "restructuredtext", ".md": "markdown"}
root_doc = "index"
myst_fence_as_directive = ["mermaid"]
myst_heading_anchors = 3

# The curated, repository-owned architecture maps link nodes to public source
# files. Mermaid disables click directives in its default strict mode.
# 'loose' is intentional here: diagram Markdown is reviewed in this repo.
mermaid_config = {"securityLevel": "loose"}

exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

breathe_projects = {
    "libaudition": str(ROOT / "docs" / "_build" / "xml")
}
breathe_default_project = "libaudition"

# Mermaid's plugin fullscreen control is replaced by a lightweight viewer
# with ordinary mouse-wheel zoom, pointer-centered scaling and drag-to-pan.
# It applies to all Mermaid diagrams, including functional_architecture.md.
mermaid_fullscreen = False

html_static_path = ["_static"]
html_js_files = ["diagram-viewer.js"]
html_css_files = ["layout.css", "diagram-viewer.css"]

html_theme = "furo"
html_title = "libaudition documentation"
html_baseurl = "https://ardev1161.github.io/libaudition/"
html_theme_options = {
    "source_repository": "https://github.com/ARDev1161/libaudition/",
    "source_branch": "documentation",
    "source_directory": "docs/",
}

# Historical reports remain searchable without appearing in the sidebar.
suppress_warnings = ["toc.not_included"]
