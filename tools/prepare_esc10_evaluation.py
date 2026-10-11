#!/usr/bin/env python3
"""Prepare a small licensed ESC-10 real-event WAV corpus for 3-model evaluation.

ESC-10 clips have CC BY licensing within ESC-50 (see upstream LICENSE).
Only selected raw WAVs are downloaded temporarily; none are committed.
Source: https://github.com/karolpiczak/ESC-50
The selected second (1.0-2.0 s) is a *proxy* for the five-second clip label:
manual verification of the event's presence in that second is still needed.
"""
import argparse
import csv
from pathlib import Path
import shutil
import subprocess
import urllib.request

UPSTREAM = "https://raw.githubusercontent.com/karolpiczak/ESC-50/master"
CATEGORIES = {
    "dog": "Dog",
    "rain": "Rain",
    "helicopter": "Helicopter",
    "chainsaw": "Chainsaw",
}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--efficientat-csv", type=Path, required=True)
    parser.add_argument("--yamnet-csv", type=Path, required=True)
    args = parser.parse_args()
    if shutil.which("ffmpeg") is None:
        parser.error("ffmpeg is required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def load_names(path):
        with path.open(newline="", encoding="utf-8-sig") as f:
            rows = list(csv.DictReader(f))
        return {row["display_name"].strip(): row["mid"].strip() for row in rows}
    eff = load_names(args.efficientat_csv)
    yam = load_names(args.yamnet_csv)
    for label in CATEGORIES.values():
        if label not in eff or label not in yam or eff[label] != yam[label]:
            raise ValueError(f"Unavailable or inconsistent AudioSet MID for {label}")

    metadata = output / "esc50.csv"
    urllib.request.urlretrieve(f"{UPSTREAM}/meta/esc50.csv", metadata)
    urllib.request.urlretrieve(f"{UPSTREAM}/LICENSE", output / "ESC-50-LICENSE.txt")
    with metadata.open(newline="", encoding="utf-8") as f:
        entries = list(csv.DictReader(f))
    chosen = {}
    for row in entries:
        category = row["category"]
        if category in CATEGORIES and row["esc10"].strip().lower() == "true" and category not in chosen:
            chosen[category] = row
    if set(chosen) != set(CATEGORIES):
        raise ValueError(f"Could not select ESC-10 clips: {set(CATEGORIES)-set(chosen)}")

    manifest = output / "manifest.csv"
    provenance = output / "provenance.csv"
    with manifest.open("w", newline="", encoding="utf-8") as manifest_stream, \
         provenance.open("w", newline="", encoding="utf-8") as provenance_stream:
        mw = csv.writer(manifest_stream)
        pw = csv.writer(provenance_stream)
        mw.writerow(["wav", "mid"])
        pw.writerow(["wav", "category", "original_filename", "source_url", "src_file", "license", "window_start_s"])
        for category, row in sorted(chosen.items()):
            name = row["filename"]
            url = f"{UPSTREAM}/audio/{name}"
            raw = output / name
            target = output / (category + ".wav")
            urllib.request.urlretrieve(url, raw)
            subprocess.run([
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-ss", "0", "-i", str(raw), "-t", "5", "-ac", "1",
                "-ar", "32000", "-c:a", "pcm_s16le", str(target)
            ], check=True)
            if category == "chainsaw":
                subprocess.run([
                    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-i", str(raw), "-t", "5", "-ac", "1", "-ar", "32000",
                    "-c:a", "pcm_s16le", str(output / "chainsaw-original.wav")
                ], check=True)
            raw.unlink()
            mw.writerow([target.name, eff[CATEGORIES[category]]])
            pw.writerow([target.name, category, name, url, row.get("src_file", ""), "CC BY (ESC-10)", 0])
    print(f"Prepared {len(chosen)} ESC-10 real-event excerpts in {output}")
    print(f"Manifest: {manifest}; attribution/provenance: {provenance}")
    print("Five-second source-level labels: YAMNet/DyMN evaluate first 1s; MN10 evaluates all 5s.")

if __name__ == "__main__":
    main()
