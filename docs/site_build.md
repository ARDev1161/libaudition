# Documentation website

The GitHub Pages site combines existing Markdown/Mermaid guides with the C++17 API. Sphinx renders the site, MyST reads Markdown, Doxygen extracts public headers as XML, and Breathe generates the C++ reference.

## Build locally

Install Doxygen (1.9.2 or newer), Graphviz (`dot`) and Python 3.12 or another supported Python 3 version, then run from the repository root:

~~~sh
python3 -m venv .venv-docs
. .venv-docs/bin/activate
python -m pip install -r docs/requirements.txt
doxygen docs/Doxyfile.public
doxygen docs/Doxyfile.pages
python3 docs/doxygen_links.py --check
sphinx-build -b html docs docs/_build/html
cp -a docs/_build/doxygen docs/_build/html/doxygen
~~~

Open docs/_build/html/index.html in a browser. The docs build does **not** enable optional C++ backends or download model weights.

Doxygen runs in two modes: **public-header XML** in `docs/_build/public_xml` for the concise Sphinx/Breathe API, and **full implementation XML** in `docs/_build/xml` together with standalone **HTML/Graphviz** output in `docs/_build/doxygen`. The final copy into `docs/_build/html/doxygen` publishes a fully navigable class/namespace/file API browser alongside Sphinx. Its public URL is [Doxygen HTML API reference](https://ardev1161.github.io/libaudition/doxygen/index.html). All generated outputs stay in the git-ignored `docs/_build` directory.

## Displayed release version

The Furo sidebar shows **libaudition documentation** as its title and the library's **public API release** (for example `v1.0.0-rc.1`) on a separate, smaller line directly beneath it. `docs/conf.py` reads `project(libaudition VERSION ...)` and `LIBAUDITION_PUBLIC_VERSION` from the repository's `CMakeLists.txt`, so prerelease suffixes are not lost. No separate website version should be maintained. The Pages workflow verifies that the rendered sidebar includes the title and separately formatted release.

## Responsive page layout

On wide desktop screens, `docs/_static/layout.css` removes Furo's narrow fixed article-width limit and lets the main text column fill the available space between the navigation sidebar and the on-page table of contents. The layout remains responsive, using Furo's original rules on smaller screens. This affects **all documentation pages**, not just Mermaid diagrams.

## Interactive Mermaid diagrams

On any documentation page containing Mermaid, **click directly on a diagram** to open its zoomable SVG viewer (no floating button). Keyboard users can focus the diagram with Tab and press **Enter** or **Space**. In the viewer:

- **Mouse wheel:** zoom in or out around the cursor (no Ctrl key required).
- **Left-button drag:** pan around a zoomed diagram.
- **+ / − buttons or keys:** zoom in or out.
- **Fit button, 0, or double-click:** reset the diagram to fit the window.
- **Escape or ×:** close the viewer and return to the original page.

Architecture diagrams also contain **repository-source links on individual nodes**: activate a linked node to open its GitHub source file in a new tab, or click the diagram background to expand it. Mermaid's default security mode disables node links; `docs/conf.py` explicitly uses `mermaid_config = {"securityLevel": "loose"}` for these reviewed, repository-maintained diagrams. Do not embed untrusted or dynamically supplied Mermaid input.

The viewer is implemented in `docs/_static/diagram-viewer.js` and `diagram-viewer.css`, enabled site-wide through `docs/conf.py`. It moves the original rendered SVG into the overlay and restores it on close, avoiding rasterization or duplicated SVG IDs. The GitHub Pages workflow checks JS syntax and verifies that Sphinx includes these assets.

## Doxygen graphs and architecture links

The standalone [Doxygen HTML reference](https://ardev1161.github.io/libaudition/doxygen/index.html) includes Graphviz-generated inheritance/class hierarchy, collaboration, include/included-by, directory, group-dependency and call/caller graphs where source relationships exist. Graphs use SVG with interactive hyperlinks. The build needs the system `graphviz` package in addition to `doxygen`. For heavily connected functions the node/depth limits in `docs/Doxyfile.pages` bound graph size.

Doxygen scans **public headers and implementations** (`include/`, `src/`, `backends/`) to document file-level source browsing as well as classes. Tests and build directories are excluded from the scan.

The [homepage subsystem diagram](index.md#modular-architecture) links directly to the **matching Doxygen file page** rather than GitHub. The source Markdown keeps reviewed repository-relative source pointers; the Sphinx `source-read` hook calls `docs/doxygen_links.py` to resolve each pointer to the exact generated Doxygen file ID from XML. If any file cannot be resolved or the HTML target is absent, the docs CI fails rather than publishing broken links. The class/interface node links to Doxygen's class index. Directly clicking a linked diagram node opens Doxygen in another tab; clicking the background opens the SVG zoom viewer.

## Publishing to GitHub Pages

The [docs-pages workflow](https://github.com/ARDev1161/libaudition/blob/documentation/.github/workflows/docs-pages.yml) builds **both** Sphinx and standalone Doxygen HTML, bundles them into one GitHub Pages artifact, and deploys on every push to the independent `documentation` branch. The entire website configuration and its workflow are maintained on that branch; no merge into `main` is required. It publishes through the official GitHub Pages Actions deployment, not a separate `gh-pages` source branch.

A repository administrator must select **Settings → Pages → Build and deployment → Source: GitHub Actions** once. The expected project-site URL is:

https://ardev1161.github.io/libaudition/

The actual site is available only after the workflow successfully deploys and GitHub Pages is enabled. If Pages was changed from **Deploy from a branch** to **GitHub Actions** after the last documentation push, make another commit to `documentation` to start the deployment workflow; changing the repository setting alone does not trigger a Sphinx build. Pushes to `documentation` run both the Sphinx/Doxygen build and the deployment. Direct pushes to `documentation` publish the site immediately. Once the small [automatic refresh workflow PR #71](https://github.com/ARDev1161/libaudition/pull/71) is merged into `main`, pushes to `main` will also rebuild and publish the website.

## Automatic updates from main

Once [PR #71](https://github.com/ARDev1161/libaudition/pull/71) is merged, the minimal `.github/workflows/docs-sync.yml` in `main` runs for every push to `main`. It checks out `documentation`, merges the new library history, builds Doxygen XML + Sphinx, pushes the synchronized source branch, and deploys the built site.

**Why both workflows deploy:** GitHub does not start another workflow from a push made using the default `GITHUB_TOKEN`. The main-branch synchronization workflow must therefore deploy its own build. Direct manual pushes to `documentation` continue to use the documentation branch's existing workflow.

The synchronization is non-destructive: it uses a normal Git merge (not a force push), prioritizes `main` for overlapping text edits, and fails instead of publishing stale content if there are unresolved conflicts or the build fails. Check the [Actions dashboard](https://github.com/ARDev1161/libaudition/actions) for failures.

Until PR #71 is merged, updates from `main` still require a manual merge:

~~~sh
git fetch origin
git switch documentation
git merge origin/main
git push origin documentation
~~~

## Maintenance

Keep user guides in docs/*.md (and docs/backends/*.md); preserve relative Markdown links and fenced Mermaid blocks. Document public C++ contracts in header comments so the API site updates automatically. Update docs/index.md when adding an important new guide or backend.

The initial CI build is a smoke gate for website generation. Existing historical reports are intentionally searchable without appearing in the primary navigation. A future warning-cleanup pass can make Sphinx warnings fatal once legacy references have been audited.
