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

## Publishing to GitHub Pages

The [docs-pages workflow](https://github.com/ARDev1161/libaudition/blob/documentation/.github/workflows/docs-pages.yml) builds documentation and deploys on every push to the independent `documentation` branch. The entire website configuration and its workflow are maintained on that branch; no merge into `main` is required. It publishes through the official GitHub Pages Actions deployment, not a separate `gh-pages` source branch.

A repository administrator must select **Settings → Pages → Build and deployment → Source: GitHub Actions** once. The expected project-site URL is:

https://ardev1161.github.io/libaudition/

The actual site is available only after the workflow successfully deploys and GitHub Pages is enabled. If Pages was changed from **Deploy from a branch** to **GitHub Actions** after the last documentation push, make another commit to `documentation` to start the deployment workflow; changing the repository setting alone does not trigger a Sphinx build. Pushes to `documentation` run both the Sphinx/Doxygen build and the deployment. Changes to `main` are **not** automatically reflected until `main` is merged into `documentation`.

## Updating documentation with recent library changes

To refresh API documentation and existing guides without modifying `main`, bring the new commits into the site branch:

~~~sh
git fetch origin
git switch documentation
git merge origin/main
git push origin documentation
~~~

Resolve any conflicting Markdown changes before pushing. The `documentation` branch remains the only publishing source.

## Maintenance

Keep user guides in docs/*.md (and docs/backends/*.md); preserve relative Markdown links and fenced Mermaid blocks. Document public C++ contracts in header comments so the API site updates automatically. Update docs/index.md when adding an important new guide or backend.

The initial CI build is a smoke gate for website generation. Existing historical reports are intentionally searchable without appearing in the primary navigation. A future warning-cleanup pass can make Sphinx warnings fatal once legacy references have been audited.
