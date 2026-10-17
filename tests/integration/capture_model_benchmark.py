#!/usr/bin/env python3
"""Execute one model stress benchmark and preserve its JSON report.

This runner deliberately does not provide mock metrics, tolerate missing
executables/models, or suppress the child's non-zero exit code.
"""
import argparse
import json
import pathlib
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("missing benchmark command")
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode:
        return result.returncode
    try:
        report = json.loads(result.stdout.strip())
    except json.JSONDecodeError as exc:
        print(f"Benchmark produced invalid JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(report, dict):
        print("Benchmark JSON must contain an object", file=sys.stderr)
        return 2
    if report.get("model") is None or report.get("sources") is None:
        print("Benchmark JSON lacks model or sources", file=sys.stderr)
        return 2
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
