#!/usr/bin/env python3
"""Prepare reproducible, attributed ESC-10 WAVs for classifier evaluation.

ESC-10 clips are CC BY (ESC-50). Clip labels describe the whole 5-second
recording, not necessarily its first second. WAVs are never committed.
"""
import argparse
import csv
from pathlib import Path
import shutil
import subprocess
import time
import urllib.error
import urllib.request

UPSTREAM = "https://raw.githubusercontent.com/karolpiczak/ESC-50/33c8ce9eb2cf0b1c2f8bcf322eb349b6be34dbb6"
CATEGORIES = {
    "dog": "Dog",
    "rain": "Rain",
    "helicopter": "Helicopter",
    "chainsaw": "Chainsaw",
}


def choose_examples(entries, per_category):
    """Return fold-balanced examples deterministically, preserving old smoke inputs."""
    if not 1 <= per_category <= 40:
        raise ValueError("clips-per-category must be between 1 and 40")
    selected = {}
    for category in sorted(CATEGORIES):
        eligible = [r for r in entries if r.get("category") == category and
                    r.get("esc10", "").strip().lower() == "true"]
        if per_category == 1:
            # Preserve the original 4-clip source selection and smoke baseline.
            if not eligible:
                raise ValueError(f"No ESC-10 examples for {category}")
            selected[category] = [eligible[0]]
            continue
        by_fold = {fold: sorted((r for r in eligible if r.get("fold") == str(fold)),
                                key=lambda row: row["filename"])
                   for fold in range(1, 6)}
        picks = []
        row_index = 0
        while len(picks) < per_category:
            added = 0
            for fold in range(1, 6):
                if row_index < len(by_fold[fold]) and len(picks) < per_category:
                    picks.append(by_fold[fold][row_index])
                    added += 1
            if not added:
                break
            row_index += 1
        if len(picks) != per_category or len({r["filename"] for r in picks}) != len(picks):
            raise ValueError(f"Not enough unique ESC-10 examples for {category}")
        selected[category] = picks
    return selected


def retrieve(url, path, retries=3):
    for attempt in range(retries):
        try:
            urllib.request.urlretrieve(url, path)
            return
        except (OSError, urllib.error.URLError):
            path.unlink(missing_ok=True)
            if attempt == retries - 1:
                raise
            time.sleep(attempt + 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--efficientat-csv", type=Path, required=True)
    parser.add_argument("--yamnet-csv", type=Path, required=True)
    parser.add_argument("--clips-per-category", type=int, default=1,
                        help="1=original four-clip smoke; up to 40 per category")
    args = parser.parse_args()
    if shutil.which("ffmpeg") is None:
        parser.error("ffmpeg is required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def load_names(path):
        with path.open(newline="", encoding="utf-8-sig") as stream:
            return {row["display_name"].strip(): row["mid"].strip()
                    for row in csv.DictReader(stream)}
    eff = load_names(args.efficientat_csv)
    yam = load_names(args.yamnet_csv)
    for label in CATEGORIES.values():
        if label not in eff or label not in yam or eff[label] != yam[label]:
            raise ValueError(f"Unavailable or inconsistent AudioSet MID for {label}")

    metadata = output / "esc50.csv"
    retrieve(f"{UPSTREAM}/meta/esc50.csv", metadata)
    retrieve(f"{UPSTREAM}/LICENSE", output / "ESC-50-LICENSE.txt")
    with metadata.open(newline="", encoding="utf-8") as stream:
        chosen = choose_examples(list(csv.DictReader(stream)), args.clips_per_category)

    manifest = output / "manifest.csv"
    provenance = output / "provenance.csv"
    with manifest.open("w", newline="", encoding="utf-8") as m_stream, \
         provenance.open("w", newline="", encoding="utf-8") as p_stream:
        mw = csv.writer(m_stream)
        pw = csv.writer(p_stream)
        mw.writerow(["wav", "mid"])
        pw.writerow(["wav", "category", "original_filename", "source_url",
                     "src_file", "license", "window_start_s", "fold"])
        for category, rows in chosen.items():
            for index, row in enumerate(rows, 1):
                name = row["filename"]
                url = f"{UPSTREAM}/audio/{name}"
                raw = output / name
                target_name = category + ".wav" if args.clips_per_category == 1 else \
                    f"{category}-{index:02d}.wav"
                target = output / target_name
                retrieve(url, raw)
                subprocess.run([
                    "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                    "-ss", "0", "-i", str(raw), "-t", "5", "-ac", "1",
                    "-ar", "32000", "-c:a", "pcm_s16le", str(target)
                ], check=True)
                if category == "chainsaw" and index == 1 and args.clips_per_category == 1:
                    subprocess.run([
                        "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                        "-i", str(raw), "-t", "5", "-ac", "1", "-ar", "32000",
                        "-c:a", "pcm_s16le", str(output / "chainsaw-original.wav")
                    ], check=True)
                raw.unlink()
                mw.writerow([target.name, eff[CATEGORIES[category]]])
                pw.writerow([target.name, category, name, url,
                             row.get("src_file", ""), "CC BY (ESC-10)", 0, row["fold"]])
    total = sum(len(rows) for rows in chosen.values())
    print(f"Prepared {total} ESC-10 recordings across {len(chosen)} classes")
    print(f"Manifest: {manifest}; provenance: {provenance}")
    print("Warning: first-second windows may not contain the clip-level event.")


if __name__ == "__main__":
    main()
