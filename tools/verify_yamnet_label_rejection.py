#!/usr/bin/env python3
"""Ensure the C++ YAMNet loader rejects silent 521-label ontology corruption."""
import argparse
import csv
import subprocess
import tempfile
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--model", type=Path, required=True)
    p.add_argument("--labels", type=Path, required=True)
    p.add_argument("--pcm16", type=Path, required=True)
    p.add_argument("--cpp-probe", type=Path, required=True)
    args = p.parse_args()

    with args.labels.open(newline="", encoding="utf-8-sig") as stream:
        originals = list(csv.DictReader(stream))
    if len(originals) != 521:
        raise ValueError("Expected official 521-class YAMNet map")

    def probe(labels):
        return subprocess.run([
            str(args.cpp_probe.resolve()), str(args.model.resolve()),
            str(labels.resolve()), str(args.pcm16.resolve())],
            capture_output=True, text=True)

    def save(path, rows):
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=("index", "mid", "display_name"))
            writer.writeheader()
            writer.writerows(rows)

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / "valid-escaped.csv"
        modified = [dict(r) for r in originals]
        modified[0]["display_name"] = 'Synthetic "quoted", event'
        save(source, modified)
        valid = probe(source)
        if valid.returncode != 0:
            raise RuntimeError(f"Valid escaped YAMNet CSV rejected: {valid.stderr}")

        for kind in ("wrong_index", "duplicate_mid", "broken_quote"):
            rows = [dict(r) for r in originals]
            path = root / (kind + ".csv")
            if kind == "wrong_index":
                rows[4]["index"] = "5"
            elif kind == "duplicate_mid":
                rows[4]["mid"] = rows[3]["mid"]
            save(path, rows)
            if kind == "broken_quote":
                content = path.read_text(encoding="utf-8")
                content = content.replace(
                    rows[0]["display_name"], '"Unterminated class', 1)
                path.write_text(content, encoding="utf-8")
            outcome = probe(path)
            if outcome.returncode == 0:
                raise RuntimeError(f"Corrupt YAMNet map accepted: {kind}")
            if "YAMNet" not in outcome.stderr:
                raise RuntimeError(f"Corrupt map rejected for wrong reason: {outcome.stderr}")
        print("PASS: escaped YAMNet labels load; shuffled indexes, duplicate MIDs and malformed quotes rejected")


if __name__ == "__main__":
    main()
