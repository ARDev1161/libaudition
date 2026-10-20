#!/usr/bin/env python3
"""Fail CI when registered model fixture digests drift from workflow checks.

Only validates manifest/workflow consistency, not upstream provenance or licensing.
"""
from pathlib import Path
from urllib.parse import urlsplit
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
manifest_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "models/manifest.yaml"
workflow_path = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / ".github/workflows/ci.yml"
if len(sys.argv) not in (1, 3):
    raise SystemExit("usage: check_model_manifest.py [MANIFEST WORKFLOW]")
manifest = manifest_path.read_text(encoding="utf-8")
workflow = workflow_path.read_text(encoding="utf-8")

entries = []
current = None
for line in manifest.splitlines():
    match = re.fullmatch(r"  - id: ([a-zA-Z0-9_.-]+)", line)
    if match:
        current = {"id": match.group(1)}
        entries.append(current)
        continue
    field = re.fullmatch(r"    (sha256|source_revision|source|bundled): (.+)", line)
    if field and current is not None:
        if field.group(1) in current:
            raise SystemExit(f"Duplicate {field.group(1)} in {current['id']}")
        current[field.group(1)] = field.group(2).strip()

if not entries:
    raise SystemExit("No registered model fixtures")
seen = set()
errors = []
for entry in entries:
    name = entry["id"]
    if name in seen:
        errors.append(f"{name}: duplicate id")
    seen.add(name)
    digest = entry.get("sha256", "")
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        errors.append(f"{name}: missing or invalid SHA-256")
    else:
        # Require a digest to appear with its expected filename on the same
        # checksum command line, not merely somewhere in the workflow.
        filename = Path(urlsplit(entry.get("source", "")).path).name
        checks = [
            line for line in workflow.splitlines()
            if re.search(r"(?<![0-9a-f])" + digest + r"(?![0-9a-f])", line)
            and filename in line
            and "sha256sum" in line
        ]
        if not checks:
            errors.append(f"{name}: SHA-256 not checked with filename {filename}")
    if not entry.get("source") or not entry.get("source_revision"):
        errors.append(f"{name}: missing source or source_revision")
    if entry.get("bundled") != "false":
        errors.append(f"{name}: CI fixture must explicitly be unbundled")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"Validated {len(entries)} unbundled model fixture digests against CI workflow")
