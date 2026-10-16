#!/usr/bin/env python3
"""Report measured, unexecuted gcovr branches without assuming reachability."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


def audit(report: dict, source_root: Path) -> dict:
    if not isinstance(report.get("files"), list) or not report["files"]:
        raise ValueError("gcovr JSON must contain non-empty files")
    files = []
    for item in report["files"]:
        filename = item.get("file")
        if not isinstance(filename, str) or not filename:
            raise ValueError("gcovr file without a path")
        path = Path(filename)
        if path.is_absolute():
            try:
                path = path.relative_to(source_root.resolve())
            except ValueError as exc:
                raise ValueError(f"coverage path outside source root: {filename}") from exc
        if ".." in path.parts:
            raise ValueError(f"coverage path escapes source root: {filename}")
        path_text = path.as_posix()
        if not path_text.startswith("src/"):
            raise ValueError(f"non-core file in core coverage: {path_text}")
        file_path = source_root / path
        if not file_path.is_file():
            raise ValueError(f"covered source file not found: {path_text}")
        code = file_path.read_text(encoding="utf-8").splitlines()
        missing = []
        covered_count = 0
        branch_count = 0
        for line in item.get("lines", []):
            number = line.get("line_number")
            if not isinstance(number, int) or not 1 <= number <= len(code):
                raise ValueError(f"invalid line in {path_text}: {number}")
            branches = line.get("branches", [])
            if not isinstance(branches, list):
                raise ValueError(f"invalid branch list in {path_text}:{number}")
            missed = []
            for index, branch in enumerate(branches):
                hits = branch.get("count")
                if not isinstance(hits, int) or hits < 0:
                    raise ValueError(f"invalid branch hits in {path_text}:{number}")
                branch_count += 1
                if hits:
                    covered_count += 1
                else:
                    missed.append(index)
            if missed:
                missing.append({
                    "line": number,
                    "uncovered_branch_indices": missed,
                    "source": code[number - 1].strip(),
                    "review_status": "unreviewed",
                })
        files.append({
            "file": path_text,
            "branch_covered": covered_count,
            "branch_total": branch_count,
            "branch_uncovered": branch_count - covered_count,
            "uncovered_locations": sorted(missing, key=lambda x: x["line"]),
        })
    if not any(f["branch_total"] for f in files):
        raise ValueError("gcovr JSON contains no branch measurements")
    files.sort(key=lambda x: (-x["branch_uncovered"], x["file"]))
    total = sum(f["branch_total"] for f in files)
    covered = sum(f["branch_covered"] for f in files)
    return {
        "kind": "Measured uncovered C++ branch triage; not a claim of reachability",
        "branch_covered": covered,
        "branch_total": total,
        "branch_uncovered": total - covered,
        "files": files,
    }


def markdown(result: dict, limit: int = 30) -> str:
    lines = [
        "# C++ branch coverage: triage required", "",
        f"Measured: {result['branch_covered']} / {result['branch_total']} "
        f"({100 * result['branch_covered'] / result['branch_total']:.1f}%). "
        f"Uncovered: {result['branch_uncovered']}.", "",
        "Uncovered means not executed by the current instrumented configuration, "
        "not necessarily reachable or a missing test. Optional backends are excluded.", "",
        "| File | Missing branches | Covered / total |",
        "| --- | ---: | ---: |",
    ]
    for entry in result["files"]:
        lines.append(f"| {entry['file']} | {entry['branch_uncovered']} | "
                     f"{entry['branch_covered']} / {entry['branch_total']} |")
    lines.extend(["", "## Next locations to review", "",
                  "| Source location | Missing branch indices | Statement |",
                  "| --- | --- | --- |"])
    candidates = [(entry["file"], location) for entry in result["files"]
                  for location in entry["uncovered_locations"]]
    candidates.sort(key=lambda x: (-len(x[1]["uncovered_branch_indices"]), x[0], x[1]["line"]))
    for filename, location in candidates[:limit]:
        code = location["source"].replace("|", r"\|").replace(chr(96), "'")
        lines.append(f"| {filename}:{location['line']} | "
                     f"{','.join(map(str, location['uncovered_branch_indices']))} | {code[:110]} |")
    lines.extend(["", "Review each line against source, existing tests, and failure modes. "
                  "Classify as testable, compiler-generated, unreachable, or excluded-backend "
                  "only with evidence. The JSON labels each gap unreviewed.", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--json-output", required=True, type=Path)
    parser.add_argument("--markdown-output", required=True, type=Path)
    parser.add_argument("--top", type=int, default=30)
    args = parser.parse_args()
    if args.top < 1:
        parser.error("--top must be positive")
    report = json.loads(args.report.read_text(encoding="utf-8"))
    result = audit(report, args.source.resolve())
    args.json_output.parent.mkdir(parents=True, exist_ok=True)
    args.markdown_output.parent.mkdir(parents=True, exist_ok=True)
    args.json_output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    args.markdown_output.write_text(markdown(result, args.top), encoding="utf-8")
    print(f"Branches: {result['branch_covered']}/{result['branch_total']}; "
          f"{result['branch_uncovered']} uncovered to triage")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"Branch audit failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
