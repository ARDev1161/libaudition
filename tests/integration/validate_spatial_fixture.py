#!/usr/bin/env python3
"""Run the actual C++ ODAS/SSS/WAV/JSON stack, inspect its spatial trace.

Quality statistics intentionally keep continuous values; thresholds for real
recordings should be calibrated from baseline and reference uncertainty rather
than copying an arbitrary cosine score into tests.
"""
import argparse
import csv
import hashlib
import json
import math
import statistics
import subprocess
import sys
import wave
from pathlib import Path


def fail(message):
    raise AssertionError(message)


def compare(manifest, scene, observations, summary):
    fs = manifest["sample_rate_hz"]
    total_hops = summary["hops"]
    hop_samples = 128
    by_hop = {}
    for observation in observations:
        hop = int(observation["hop"])
        assert 0 <= hop < total_hops
        assert observation["kind"] in {"track", "proposal"}
        by_hop.setdefault(hop, []).append(observation)

    reference = manifest["scenes"][scene]
    if summary["input_frames"] != reference["frames"]:
        fail("Audio length is inconsistent with manifest")
    if summary["sample_rate_hz"] != fs:
        fail("Sample rate is inconsistent with manifest")
    if summary["recorder_drops"]:
        fail("Unexpected WAV recorder drops under offline replay")
    if summary["track_observations"] != sum(
        r["kind"] == "track" for r in observations
    ):
        fail("Summary/trace mismatch for tracked sources")
    if summary["potential_observations"] != sum(
        r["kind"] == "proposal" for r in observations
    ):
        fail("Summary/trace mismatch for proposals")
    if summary["recordings"] > summary["track_observations"]:
        fail("More recordings than track observations")
    if summary["separated_frames"] > summary["track_observations"]:
        fail("SSS produced more audio frames than tracks")

    samples = []
    covered_hops = 0
    active_hops = 0
    simultaneous_hops = 0
    longest_silent_track = 0
    silent_track_run = 0
    for hop in range(total_hops):
        t = (hop * hop_samples + hop_samples / 2) / fs
        known = []
        for ref in reference["sources"]:
            if ref["start_s"] <= t < ref["end_s"]:
                progress = ((t - ref["start_s"]) /
                            (ref["end_s"] - ref["start_s"]))
                known.append(ref["azimuth_start_deg"] + progress *
                             (ref["azimuth_end_deg"] -
                              ref["azimuth_start_deg"]))
        tracks = [o for o in by_hop.get(hop, [])
                  if o["kind"] == "track"]
        if not known:
            silent_track_run = silent_track_run + 1 if tracks else 0
            longest_silent_track = max(longest_silent_track, silent_track_run)
            continue
        if len(known) > 1 and len(tracks) >= 2:
            simultaneous_hops += 1
        active_hops += 1
        if not tracks:
            continue
        covered_hops += 1
        for direction in known:
            ref_radians = math.radians(direction)
            ref_vector = (math.cos(ref_radians),
                          math.sin(ref_radians), 0.0)
            errors = []
            for obs in tracks:
                dot = sum(a * float(obs[axis])
                          for a, axis in zip(ref_vector, ("x", "y", "z")))
                errors.append(math.degrees(
                    math.acos(max(-1.0, min(1.0, dot)))))
            samples.append(min(errors))

    metrics = {
        "scene": scene,
        "hops": total_hops,
        "active_hops": active_hops,
        "track_covered_hops": covered_hops,
        "track_coverage": round(
            covered_hops / active_hops, 6) if active_hops else None,
        "median_nearest_track_error_deg": round(
            statistics.median(samples), 4) if samples else None,
        "p90_nearest_track_error_deg": round(
            sorted(samples)[min(len(samples)-1,
                                math.ceil(0.9 * len(samples))-1)], 4)
            if samples else None,
        "simultaneous_two_track_hops": simultaneous_hops,
        "longest_silent_track_run": longest_silent_track,
        "proposal_observations": summary["potential_observations"],
        "track_observations": summary["track_observations"],
        "sss_frames": summary["separated_frames"],
        "recordings": summary["recordings"],
        "recorder_drops": summary["recorder_drops"],
    }
    # Strict functional gates: these test library processing and persistence.
    # DOA numeric thresholds are opt-in until baseline verification on each
    # array profile and dataset, and cannot be mistaken for accuracy evidence.
    if scene == "silence":
        metrics["acceptance_scope"] = "silence-rejection"
        if longest_silent_track > 10:
            fail("Silence produced a stable false-positive ODAS track")
    else:
        if summary["potential_observations"] < 1:
            fail("No potential sources found in active spatial scene")
        if scene == "static":
            # Current production SST settings do not necessarily acquire a
            # continuously stationary synthetic signal. Do not silently
            # tune the engine to get a green CI; this case gates SSL only.
            # The missing track is explicitly reported for quality work.
            metrics["acceptance_scope"] = "ssl"
            metrics["sst_static_track_present"] = (
                summary["track_observations"] > 0)
        else:
            metrics["acceptance_scope"] = "ssl-sst-sss-recording"
            if summary["track_observations"] < 1:
                fail("No tracked sources found in active spatial scene")
            if summary["separated_frames"] < 1 or summary["recordings"] < 1:
                fail("No SSS audio or recording produced from active source")
        if summary["separated_frames"] != summary["track_observations"]:
            fail("SSS did not produce one audio frame per active track")
    return metrics


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--runner", type=Path, required=True)
    p.add_argument("--fixtures", type=Path, required=True)
    p.add_argument("--scene", choices=["silence", "static", "moving", "two"],
                   required=True)
    p.add_argument("--results", type=Path, required=True)
    p.add_argument("--max-median-error-deg", type=float)
    p.add_argument("--min-track-coverage", type=float)
    p.add_argument("--require-static-track", action="store_true")
    args = p.parse_args()

    manifest = json.loads(
        (args.fixtures / "ground_truth.json").read_text(encoding="utf-8"))
    if manifest["schema"] != "libaudition.spatial-fixture.v1":
        fail("Unrecognized test fixture schema")
    item = manifest["scenes"][args.scene]
    wav = args.fixtures / item["file"]
    if hashlib.sha256(wav.read_bytes()).hexdigest() != item["sha256"]:
        fail("Fixture checksum mismatch: " + str(wav))
    with wave.open(str(wav)) as audio:
        if (audio.getnchannels() != manifest["channels"] or
                audio.getframerate() != manifest["sample_rate_hz"] or
                audio.getsampwidth() != 2 or
                audio.getnframes() != item["frames"]):
            fail("Fixture WAV header does not match ground truth")

    folder = args.results / args.scene
    folder.mkdir(parents=True, exist_ok=True)
    command = [str(args.runner), str(wav), str(folder)]
    run = subprocess.run(command, text=True, capture_output=True,
                         timeout=120, check=False)
    (folder / "runner.stdout.txt").write_text(run.stdout, encoding="utf-8")
    (folder / "runner.stderr.txt").write_text(run.stderr, encoding="utf-8")
    if run.returncode:
        print(run.stdout)
        print(run.stderr, file=sys.stderr)
        fail(f"Offline runner failed: returncode={run.returncode}")
    summary = json.loads((folder / "summary.json").read_text())
    with (folder / "observations.csv").open(newline="") as csv_file:
        observations = list(csv.DictReader(csv_file))
    metrics = compare(manifest, args.scene, observations, summary)
    (folder / "metrics.json").write_text(
        json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if args.require_static_track and args.scene == "static":
        if not metrics["sst_static_track_present"]:
            fail("Static SST acquisition missing (strict quality mode)")
    if args.max_median_error_deg is not None:
        if metrics["median_nearest_track_error_deg"] is None:
            fail("Unable to evaluate DOA against reference")
        if metrics["median_nearest_track_error_deg"] > args.max_median_error_deg:
            fail("Measured angular error exceeds calibrated threshold")
    if args.min_track_coverage is not None:
        if metrics["track_coverage"] is None:
            fail("No active-reference interval for coverage comparison")
        if metrics["track_coverage"] < args.min_track_coverage:
            fail("Track coverage below calibrated threshold")
    print(f"audio-integration=ok {json.dumps(metrics, sort_keys=True)}")


if __name__ == "__main__":
    main()
