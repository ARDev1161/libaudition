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

# This is intentionally a constrained, dependency-free YAML subset. Never
# silently ignore unknown keys, indentation or extra model records: doing so
# would allow a fixture to escape the checksum and licensing audit.
entries = []
current = None
schema_count = 0
models_count = 0
for number, line in enumerate(manifest.splitlines(), 1):
    if not line.strip() or line.lstrip().startswith("#"):
        continue
    if line == "schema_version: 1":
        schema_count += 1
        continue
    if line == "models:":
        models_count += 1
        continue
    match = re.fullmatch(r"  - id: ([a-zA-Z0-9_.-]+)", line)
    if match and models_count == 1:
        current = {"id": match.group(1)}
        entries.append(current)
        continue
    field = re.fullmatch(r"    (sha256|source_revision|source|ci_filename|bundled|backend|task|license|license_review|license_evidence|commercial|redistributable|purpose): (.+)", line)
    if field and current is not None and models_count == 1:
        if field.group(1) in current:
            raise SystemExit(f"Duplicate {field.group(1)} in {current['id']}")
        current[field.group(1)] = field.group(2).strip()
        continue
    raise SystemExit(f"Unsupported model manifest syntax at line {number}: {line!r}")

if schema_count != 1 or models_count != 1:
    raise SystemExit("Model manifest requires exactly one schema_version: 1 and models: section")
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
        filename = entry.get("ci_filename", Path(urlsplit(entry.get("source", "")).path).name)
        if "ci_filename" in entry and not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", filename):
            errors.append(f"{name}: ci_filename must be a safe basename")
        digest_pattern = re.compile(r"(?<![0-9a-f])" + digest + r"(?![0-9a-f])")
        checks = []
        for line in workflow.splitlines():
            if line.lstrip().startswith("#"):
                continue
            if not re.search(r"\|\s*sha256sum\s+(?:--check|-c)(?:\s|$)", line):
                continue
            producer = line.split("|", 1)[0]
            found = digest_pattern.search(producer)
            if found:
                # Match the checksum operand, not an unrelated later mention.
                operand = producer[found.end():]
                # CI can construct checksum paths with shell variables and
                # adjacent quote fragments. Compare the first checksum
                # operand after removing quote delimiters, not later text.
                record = operand.replace(chr(39), "").replace(chr(34), "").strip()
                checked_path = record.split(maxsplit=1)[0] if record else ""
                if Path(checked_path).name == filename:
                    checks.append(line)
        if not checks:
            errors.append(f"{name}: SHA-256 not checked with filename {filename}")
    if not entry.get("source") or not entry.get("source_revision"):
        errors.append(f"{name}: missing source or source_revision")
    source = entry.get("source", "")
    revision = entry.get("source_revision", "")
    parsed = urlsplit(source)
    if parsed.scheme != "https" or not parsed.netloc:
        errors.append(f"{name}: source must be HTTPS")
    resolved = re.search(r"/resolve/([^/]+)/", parsed.path)
    if resolved:
        ref = resolved.group(1)
        expected = "UNPINNED-main" if ref == "main" else ref
        if revision != expected:
            errors.append(f"{name}: source_revision does not match source URL")
    elif revision.startswith("UNPINNED-"):
        errors.append(f"{name}: unpinned revision without matching URL")
    for field in ("backend", "task", "license", "purpose"):
        if not entry.get(field):
            errors.append(f"{name}: missing {field}")
    for field in ("commercial", "redistributable"):
        if entry.get(field) not in ("true", "false"):
            errors.append(f"{name}: invalid {field}")
    review = entry.get("license_review")
    if review not in ("pending", "approved"):
        errors.append(f"{name}: license_review must be pending or approved")
    if entry.get("license") == "UNVERIFIED" and review != "pending":
        errors.append(f"{name}: unverified license requires pending review")
    if review == "pending" and (entry.get("commercial") == "true" or entry.get("redistributable") == "true"):
        errors.append(f"{name}: pending review cannot grant model use or redistribution")
    if review == "approved" and (revision.startswith("UNPINNED-") or "/resolve/main/" in source):
        errors.append(f"{name}: approved fixture requires immutable source revision")
    if review == "approved" and resolved and not re.fullmatch(r"[0-9a-f]{40}", resolved.group(1)):
        errors.append(f"{name}: approved resolve source requires full 40-character commit SHA")
    if review == "approved":
        evidence = entry.get("license_evidence", "")
        # Existence is a manifest consistency gate, not independent legal approval.
        if not re.fullmatch(r"docs/legal/[A-Za-z0-9][A-Za-z0-9_.-]*\.md", evidence):
            errors.append(f"{name}: approved review requires docs/legal/*.md license_evidence")
        else:
            repository_root = (manifest_path.parent.parent if manifest_path.parent.name == "models"
                               else manifest_path.parent).resolve()
            legal_root = (repository_root / "docs/legal").resolve()
            try:
                evidence_file = (repository_root / evidence).resolve(strict=True)
                if (not evidence_file.is_relative_to(legal_root)
                        or not evidence_file.is_file()
                        or evidence_file.stat().st_size == 0):
                    errors.append(f"{name}: license_evidence must be a nonempty repo-local legal file")
            except (OSError, ValueError):
                errors.append(f"{name}: license_evidence document does not exist")
    if entry.get("bundled") != "false":
        errors.append(f"{name}: CI fixture must explicitly be unbundled")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"Validated {len(entries)} unbundled model fixture digests against CI workflow")
