# Documentation website

The GitHub Pages site combines existing Markdown/Mermaid guides with the C++17 API. Sphinx renders the site, MyST reads Markdown, Doxygen extracts public headers as XML, and Breathe generates the C++ reference.

## Build locally

Install Doxygen (1.9.2 or newer) and Python 3.12 or another supported Python 3 version, then run from the repository root:

~~~sh
python3 -m venv .venv-docs
. .venv-docs/bin/activate
python -m pip install -r docs/requirements.txt
doxygen docs/Doxyfile.pages
sphinx-build -b html docs docs/_build/html
~~~

Open docs/_build/html/index.html in a browser. The docs build does **not** enable optional C++ backends or download model weights.

Doxygen outputs XML into docs/_build/xml. Sphinx reads that directory through Breathe. Both build outputs belong in docs/_build, which must remain ignored by git.

## Interactive Mermaid diagrams

On any documentation page containing Mermaid, use the **⛶** button in the corner of the diagram to open the zoomable SVG viewer. In that viewer:

- **Mouse wheel:** zoom in or out around the cursor (no Ctrl key required).
- **Left-button drag:** pan around a zoomed diagram.
- **+ / − buttons or keys:** zoom in or out.
- **Fit button, 0, or double-click:** reset the diagram to fit the window.
- **Escape or ×:** close the viewer and return to the original page.

The viewer is implemented in `docs/_static/diagram-viewer.js` and `diagram-viewer.css`, enabled site-wide through `docs/conf.py`. It moves the original rendered SVG into the overlay and restores it on close, avoiding rasterization or duplicated SVG IDs. The GitHub Pages workflow checks JS syntax and verifies that Sphinx includes these assets.

## Publishing to GitHub Pages

The [docs-pages workflow](https://github.com/ARDev1161/libaudition/blob/documentation/.github/workflows/docs-pages.yml) builds documentation and deploys on every push to the independent `documentation` branch. The entire website configuration and its workflow are maintained on that branch; no merge into `main` is required. It publishes through the official GitHub Pages Actions deployment, not a separate `gh-pages` source branch.

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
