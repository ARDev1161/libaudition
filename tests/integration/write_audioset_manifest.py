#!/usr/bin/env python3
"""Write a deterministic single-event AudioSet manifest for model CI."""
from __future__ import annotations

import argparse
import csv
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wav", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mid", default="/m/09x0r")
    args = parser.parse_args()

    if not args.wav.is_file() or args.wav.stat().st_size == 0:
        parser.error(f"missing or empty WAV: {args.wav}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as dest:
        writer = csv.writer(dest, lineterminator="\n")
        writer.writerow(("wav", "mid"))
        writer.writerow((str(args.wav.resolve()), args.mid))


if __name__ == "__main__":
    main()
