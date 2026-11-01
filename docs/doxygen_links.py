"""Resolve homepage Mermaid source links to Doxygen HTML file pages.

Doxygen IDs depend on paths and may change as the source tree grows. Read its
generated XML index rather than guessing HTML names or hardcoding GitHub links.

Run after "doxygen docs/Doxyfile.pages":
    python3 docs/doxygen_links.py --check
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import xml.etree.ElementTree as ET


DOCS_DIR = Path(__file__).resolve().parent
BUILD_DIR = DOCS_DIR / "_build"
# A semantic placeholder is kept in the repository; Sphinx replaces it
# with the exact generated HTML path before Mermaid renders the diagram.
SOURCE_LINK = re.compile(
    r'(?m)^(?P<begin>\s*click\s+\w+\s+)"doxygen-file:'
    + r'(?P<path>[^"]+)"(?P<end>[^\n]*)


def _file_pages(build_dir: Path) -> dict[str, str]:
    xml_dir = build_dir / "xml"
    index_path = xml_dir / "index.xml"
    if not index_path.is_file():
        raise RuntimeError(f"Doxygen XML missing: {index_path}")

    index = ET.parse(index_path).getroot()
    pages: dict[str, str] = {}
    for entry in index.findall("compound"):
        if entry.get("kind") != "file":
            continue
        refid = entry.get("refid")
        if not refid:
            continue
        detail_path = xml_dir / f"{refid}.xml"
        if not detail_path.is_file():
            continue
        compound = ET.parse(detail_path).find("compounddef")
        if compound is None:
            continue
        location = compound.find("location")
        if location is None or not location.get("file"):
            continue
        raw_path = location.attrib["file"].replace("\\", "/").lstrip("./")
        # Doxygen may emit absolute paths on the GitHub Actions runner, or
        # relative paths when STRIP_FROM_PATH is configured.
        file_html = build_dir / "doxygen" / f"{refid}.html"
        if not file_html.is_file():
            raise RuntimeError(f"Doxygen HTML for {raw_path} missing: {file_html}")
        pages[raw_path] = f"doxygen/{refid}.html"
    return pages


def rewrite_architecture_links(source: str, build_dir: Path = BUILD_DIR) -> str:
    """Resolve each doxygen-file: placeholder in the Mermaid block.

    Strict resolution is intentional: a source move must fail CI instead of
    leaving a broken diagram link. Link other external Markdown normally.
    """
    pages = _file_pages(build_dir)
    matched = 0

    def replace(match: re.Match[str]) -> str:
        nonlocal matched
        target = match.group("path")
        matches = {
            url
            for path, url in pages.items()
            if path == target or path.endswith("/" + target)
        }
        if not matches:
            # Some Doxygen versions emit only the basename in <location>.
            # Accept that fallback only when it is unambiguous.
            basename = target.rsplit("/", 1)[-1]
            matches = {
                url for path, url in pages.items()
                if path.rsplit("/", 1)[-1] == basename
            }
        if len(matches) != 1:
            raise RuntimeError(
                f"Cannot uniquely resolve {target} to a Doxygen HTML page "
                f"(found {len(matches)}). Check docs/Doxyfile.pages INPUT."
            )
        matched += 1
        return match.group("begin") + '"' + next(iter(matches)) + '"' + match.group("end")

    result = SOURCE_LINK.sub(replace, source)
    if matched < 20:
        raise RuntimeError(
            f"Expected at least 20 architecture file links, resolved {matched}."
        )
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="Validate every Mermaid source link and generated HTML target")
    args = parser.parse_args()
    index = (DOCS_DIR / "index.md").read_text(encoding="utf-8")
    resolved = rewrite_architecture_links(index)
    count = len(re.findall(r'^\s*click\s+\w+\s+"doxygen/[^"]+\.html"', resolved, re.M))
    if args.check:
        print(f"Validated {count} diagram links against generated Doxygen HTML.")
    else:
        print(resolved)


if __name__ == "__main__":
    main()

)


def _file_pages(build_dir: Path) -> dict[str, str]:
    xml_dir = build_dir / "xml"
    index_path = xml_dir / "index.xml"
    if not index_path.is_file():
        raise RuntimeError(f"Doxygen XML missing: {index_path}")

    index = ET.parse(index_path).getroot()
    pages: dict[str, str] = {}
    for entry in index.findall("compound"):
        if entry.get("kind") != "file":
            continue
        refid = entry.get("refid")
        if not refid:
            continue
        detail_path = xml_dir / f"{refid}.xml"
        if not detail_path.is_file():
            continue
        compound = ET.parse(detail_path).find("compounddef")
        if compound is None:
            continue
        location = compound.find("location")
        if location is None or not location.get("file"):
            continue
        raw_path = location.attrib["file"].replace("\\", "/").lstrip("./")
        # Doxygen may emit absolute paths on the GitHub Actions runner, or
        # relative paths when STRIP_FROM_PATH is configured.
        file_html = build_dir / "doxygen" / f"{refid}.html"
        if not file_html.is_file():
            raise RuntimeError(f"Doxygen HTML for {raw_path} missing: {file_html}")
        pages[raw_path] = f"doxygen/{refid}.html"
    return pages


def rewrite_architecture_links(source: str, build_dir: Path = BUILD_DIR) -> str:
    """Replace every github.com source link in the Mermaid block.

    Strict resolution is intentional: a source move must fail CI instead of
    leaving a broken diagram link. Link other external Markdown normally.
    """
    pages = _file_pages(build_dir)
    matched = 0

    def replace(match: re.Match[str]) -> str:
        nonlocal matched
        target = match.group("path")
        matches = {
            url
            for path, url in pages.items()
            if path == target or path.endswith("/" + target)
        }
        if not matches:
            # Some Doxygen versions emit only the basename in <location>.
            # Accept that fallback only when it is unambiguous.
            basename = target.rsplit("/", 1)[-1]
            matches = {
                url for path, url in pages.items()
                if path.rsplit("/", 1)[-1] == basename
            }
        if len(matches) != 1:
            raise RuntimeError(
                f"Cannot uniquely resolve {target} to a Doxygen HTML page "
                f"(found {len(matches)}). Check docs/Doxyfile.pages INPUT."
            )
        matched += 1
        return match.group("begin") + '"' + next(iter(matches)) + '"' + match.group("end")

    result = SOURCE_LINK.sub(replace, source)
    if matched < 20:
        raise RuntimeError(
            f"Expected at least 20 architecture file links, resolved {matched}."
        )
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="Validate every Mermaid source link and generated HTML target")
    args = parser.parse_args()
    index = (DOCS_DIR / "index.md").read_text(encoding="utf-8")
    resolved = rewrite_architecture_links(index)
    count = len(re.findall(r'^\s*click\s+\w+\s+"doxygen/[^"]+\.html"', resolved, re.M))
    if args.check:
        print(f"Validated {count} diagram links against generated Doxygen HTML.")
    else:
        print(resolved)


if __name__ == "__main__":
    main()
