"""Sphinx website for libaudition.

Build Doxygen XML first: doxygen docs/Doxyfile.pages
Then run: sphinx-build -b html docs docs/_build/html
"""

from pathlib import Path
import re
import sys

# Sphinx executes conf.py without guaranteeing docs/ on sys.path.
# The source-read hook imports the adjacent doxygen_links.py module.
DOCS_DIR = Path(__file__).resolve().parent
if str(DOCS_DIR) not in sys.path:
    sys.path.insert(0, str(DOCS_DIR))

ROOT = DOCS_DIR.parent
project = "libaudition"
author = "libaudition contributors"

cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
version_match = re.search(r"project\(libaudition VERSION ([0-9.]+)", cmake_text)
version = version_match.group(1) if version_match else "dev"
# The CMake package version is numeric, but public API releases may be
# prereleases such as 1.0.0-rc.1. Display the actual public identity.
public_version_match = re.search(
    r'set\(LIBAUDITION_PUBLIC_VERSION\s+"([^"]+)"\)', cmake_text
)
release = (
    public_version_match.group(1).replace("${PROJECT_VERSION}", version)
    if public_version_match
    else version
)

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
    "libaudition": str(ROOT / "docs" / "_build" / "public_xml")
}
breathe_default_project = "libaudition"

# Mermaid's plugin fullscreen control is replaced by a lightweight viewer
# with ordinary mouse-wheel zoom, pointer-centered scaling and drag-to-pan.
# It applies to all Mermaid diagrams, including functional_architecture.md.
mermaid_fullscreen = False

html_templates_path = ["_templates"]
html_static_path = ["_static"]
html_js_files = ["diagram-viewer.js"]
html_css_files = ["layout.css", "diagram-viewer.css"]

html_theme = "furo"
# Use a uniquely named template in html_sidebars: Furo's inherited
# sidebar/brand.html otherwise takes precedence over template overrides.
# Preserve Furo's normal search, navigation, and theme controls.
html_sidebars = {
    "**": [
        "sidebar/libaudition-brand.html",
        "sidebar/search.html",
        "sidebar/scroll-start.html",
        "sidebar/navigation.html",
        "sidebar/ethical-ads.html",
        "sidebar/scroll-end.html",
        "sidebar/variant-selector.html",
    ]
}
html_title = "libaudition documentation"
# Render the public release as its own smaller line beneath the Furo brand.
html_context = {"libaudition_sidebar_version": f"v{release}"}
html_baseurl = "https://ardev1161.github.io/libaudition/"
html_theme_options = {
    "source_repository": "https://github.com/ARDev1161/libaudition/",
    "source_branch": "documentation",
    "source_directory": "docs/",
}

# Historical reports remain searchable without appearing in the sidebar.
suppress_warnings = ["toc.not_included"]

# Map Mermaid nodes to the generated Doxygen file pages using XML refids.
# Doxygen must finish before Sphinx starts. Never embed unstable guessed URLs.
def _resolve_diagram_links(app, docname, source):
    if docname != "index" or app.builder.format != "html":
        return
    from doxygen_links import rewrite_architecture_links
    source[0] = rewrite_architecture_links(source[0], ROOT / "docs" / "_build")


def setup(app):
    app.connect("source-read", _resolve_diagram_links)
