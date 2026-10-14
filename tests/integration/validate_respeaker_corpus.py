#!/usr/bin/env python3
"""Validate an independently annotated, real six-channel ReSpeaker capture corpus.

Corpus JSON v1:
{"schema":"libaudition.respeaker-corpus.v1",
 "recordings":[{"id":"test01","wav":"test01.wav","sha256":"<64 hex>",
 "intervals":[{"start_s":1.0,"end_s":3.0,"azimuth_deg":45.0}]}]}
Angles are measured in the microphone-frame convention of the runner:
atan2(y,x), degrees. Each reference interval describes one stationary source.
Recordings must be 6-channel, 16-kHz, 16-bit PCM and an integer number
of 128-frame hops. Use an independent measurement for reference azimuths.
"""
import argparse
import csv
import hashlib
import json
import math
import re
import statistics
import subprocess
import sys
import wave
from pathlib import Path

HOP = 128
FS = 16000


def check(condition, message):
    if not condition:
        raise ValueError(message)


def angular_error(row, azimuth):
    a = math.radians(azimuth)
    cosine = float(row["x"]) * math.cos(a) + float(row["y"]) * math.sin(a)
    return math.degrees(math.acos(max(-1.0, min(1.0, cosine))))


def validate_manifest(path):
    document = json.loads(path.read_text(encoding="utf-8"))
    check(document.get("schema") == "libaudition.respeaker-corpus.v1",
          "Unknown corpus schema")
    recordings = document.get("recordings")
    check(isinstance(recordings, list) and recordings, "Empty corpus")
    ids = set()
    for rec in recordings:
        name = rec.get("id")
        check(isinstance(name, str) and re.fullmatch(r"[A-Za-z0-9_-]{1,64}", name),
              "Invalid recording id")
        check(name not in ids, "Duplicate recording id")
        ids.add(name)
        wav = rec.get("wav")
        check(isinstance(wav, str) and wav.endswith(".wav"), "Invalid WAV filename")
        p = Path(wav)
        check(not p.is_absolute() and ".." not in p.parts, "WAV path escapes corpus")
        checksum = rec.get("sha256")
        check(isinstance(checksum, str) and re.fullmatch("[0-9a-f]{64}", checksum),
              "A SHA-256 checksum is required for each recording")
        intervals = rec.get("intervals")
        check(isinstance(intervals, list) and intervals, "Missing ground truth")
        last_end = 0.0
        for interval in intervals:
            start, end, azimuth = (interval.get(x) for x in
                                   ("start_s", "end_s", "azimuth_deg"))
            check(all(isinstance(x, (int, float)) and
                      not isinstance(x, bool) and math.isfinite(x)
                      for x in (start, end, azimuth)), "Nonfinite annotation")
            check(0 <= start < end and start >= last_end,
                  "Invalid or overlapping reference intervals")
            check(-180 <= azimuth <= 180, "Azimuth must be in degrees")
            last_end = end
    return recordings


def evaluate(rows, intervals, total_hops):
    observed = {}
    for row in rows:
        hop = int(row["hop"])
        check(0 <= hop < total_hops, "Observation outside recording")
        check(row["kind"] in ("track", "proposal"), "Unknown observation kind")
        observed.setdefault(hop, []).append(row)
    metrics = []
    for interval in intervals:
        start_hop = math.ceil(float(interval["start_s"]) * FS / HOP)
        end_hop = math.floor(float(interval["end_s"]) * FS / HOP)
        check(end_hop > start_hop, "Annotation interval shorter than a hop")
        bearing = float(interval["azimuth_deg"])
        tracks = []
        proposals = []
        covered = 0
        for hop in range(start_hop, end_hop):
            data = observed.get(hop, [])
            ts = [row for row in data if row["kind"] == "track"]
            ps = [row for row in data if row["kind"] == "proposal"]
            if ts:
                covered += 1
                tracks.append(min(angular_error(row, bearing) for row in ts))
            if ps:
                proposals.append(min(angular_error(row, bearing) for row in ps))
        metrics.append({
            "start_s": interval["start_s"],
            "end_s": interval["end_s"],
            "reference_azimuth_deg": bearing,
            "track_coverage": covered / (end_hop - start_hop),
            "median_track_error_deg": statistics.median(tracks) if tracks else None,
            "median_proposal_error_deg": statistics.median(proposals) if proposals else None,
            "track_error_samples": len(tracks),
        })
    return metrics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--max-median-error-deg", type=float)
    parser.add_argument("--min-track-coverage", type=float)
    parser.add_argument("--horizontal-sources", action="store_true")
    args = parser.parse_args()
    if args.max_median_error_deg is not None:
        check(0 <= args.max_median_error_deg <= 180, "Invalid angular threshold")
    if args.min_track_coverage is not None:
        check(0 <= args.min_track_coverage <= 1, "Invalid coverage threshold")
    check(args.runner.is_file(), "Missing ODAS runner")
    check(args.manifest.is_file(), "Missing corpus manifest")
    corpus = validate_manifest(args.manifest)
    args.results.mkdir(parents=True, exist_ok=True)
    report = {"schema": "libaudition.respeaker-evaluation.v1", "recordings": []}
    for rec in corpus:
        wav_path = (args.manifest.parent / rec["wav"]).resolve()
        check(wav_path.is_file(), "WAV not found: " + rec["wav"])
        check(hashlib.sha256(wav_path.read_bytes()).hexdigest() == rec["sha256"],
              "WAV checksum mismatch: " + rec["wav"])
        with wave.open(str(wav_path)) as stream:
            check(stream.getnchannels() == 6 and stream.getframerate() == FS
                  and stream.getsampwidth() == 2 and
                  stream.getcomptype() == "NONE", "Expected 6-channel PCM16 16kHz")
            frames = stream.getnframes()
        check(frames > 0 and frames % HOP == 0, "WAV must contain full ODAS hops")
        for interval in rec["intervals"]:
            check(interval["end_s"] <= frames / FS,
                  "Ground truth extends beyond WAV duration")
        dest = args.results / rec["id"]
        dest.mkdir(parents=True, exist_ok=True)
        cmd = [str(args.runner), str(wav_path), str(dest)]
        if args.horizontal_sources:
            cmd.append("--horizontal-sources")
        run = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        (dest / "runner.stdout.txt").write_text(run.stdout, encoding="utf-8")
        (dest / "runner.stderr.txt").write_text(run.stderr, encoding="utf-8")
        check(run.returncode == 0,
              "ODAS runner failed for " + rec["id"] + ": " + run.stderr[-2000:])
        summary = json.loads((dest / "summary.json").read_text())
        check(summary["input_frames"] == frames and summary["sample_rate_hz"] == FS,
              "Runner summary inconsistent with source WAV")
        check(summary["recorder_drops"] == 0, "Dropped source recording frames")
        with (dest / "observations.csv").open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        segments = evaluate(rows, rec["intervals"], summary["hops"])
        item = {"id": rec["id"], "wav_sha256": rec["sha256"],
                "segments": segments, "summary": summary}
        report["recordings"].append(item)
        (dest / "metrics.json").write_text(
            json.dumps(item, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        for segment in segments:
            if args.max_median_error_deg is not None:
                value = segment["median_track_error_deg"]
                check(value is not None and value <= args.max_median_error_deg,
                      "DOA angular acceptance failed: " + rec["id"])
            if args.min_track_coverage is not None:
                check(segment["track_coverage"] >= args.min_track_coverage,
                      "Track coverage acceptance failed: " + rec["id"])
    (args.results / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("respeaker-corpus=ok recordings=" + str(len(corpus)))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, AssertionError,
            subprocess.TimeoutExpired) as error:
        print("respeaker-corpus=fail: " + str(error), file=sys.stderr)
        sys.exit(1)
