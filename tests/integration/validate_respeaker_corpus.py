#!/usr/bin/env python3
"""Validate an independently annotated, real six-channel ReSpeaker capture corpus.

Corpus JSON v1:
{"schema":"libaudition.respeaker-corpus.v1",
 "recordings":[{"id":"test01","wav":"test01.wav","sha256":"<64 hex>",
 "intervals":[{"start_s":1.0,"end_s":3.0,"azimuth_deg":45.0}]}]}
Angles are measured in the microphone-frame convention of the runner:
atan2(y,x), degrees. Each interval describes a source (optional source_id) with either static
azimuth_deg or linearly changing azimuth_start_deg/azimuth_end_deg.
Multiple sources may overlap; matches are one-to-one per analysis hop.
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


def angular_error(row, reference):
    # Float references are legacy horizontal azimuths. Unit 3D vectors
    # retain LOCATA elevation without silently projecting to the XY plane.
    if isinstance(reference, (int, float)):
        a = math.radians(reference)
        vector = (math.cos(a), math.sin(a), 0.0)
    else:
        vector = reference
    cosine = sum(float(row[axis]) * expected for axis, expected
                 in zip(("x", "y", "z"), vector))
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
        per_source_end = {}
        for index, interval in enumerate(intervals):
            check(isinstance(interval, dict), "Interval must be an object")
            source = interval.get("source_id", "default")
            check(isinstance(source, str) and
                  re.fullmatch(r"[A-Za-z0-9_-]{1,64}", source),
                  "Invalid source_id")
            start, end = interval.get("start_s"), interval.get("end_s")
            check(all(isinstance(x, (int, float)) and
                      not isinstance(x, bool) and math.isfinite(x)
                      for x in (start, end)), "Nonfinite interval timestamp")
            check(0 <= start < end and start >= per_source_end.get(source, 0),
                  "Overlapping or unordered intervals for the same source")
            per_source_end[source] = end
            static = "azimuth_deg" in interval
            moving = ("azimuth_start_deg" in interval or
                      "azimuth_end_deg" in interval)
            full_3d = "direction_xyz" in interval
            check(sum((static, moving, full_3d)) == 1,
                  "Specify static azimuth, moving azimuth OR unit 3D direction")
            if full_3d:
                vector = interval["direction_xyz"]
                check(isinstance(vector, list) and len(vector) == 3 and
                      all(isinstance(v, (int, float)) and
                          not isinstance(v, bool) and math.isfinite(v)
                          for v in vector) and
                      abs(sum(v * v for v in vector) - 1.0) <= 1e-4,
                      "Invalid 3D unit direction")
            else:
                keys = (("azimuth_deg",) if static else
                        ("azimuth_start_deg", "azimuth_end_deg"))
                for key in keys:
                    azimuth = interval.get(key)
                    check(isinstance(azimuth, (int, float)) and
                          not isinstance(azimuth, bool) and math.isfinite(azimuth) and
                          -180 <= azimuth <= 180, "Invalid azimuth: " + key)
            check(math.floor(end * FS / HOP) > math.ceil(start * FS / HOP),
                  "Annotation must include at least one full hop")
    array = document.get("array")
    if array is not None:
        check(isinstance(array, dict), "array must be an object")
        microphones = array.get("microphones")
        check(isinstance(microphones, list) and len(microphones) >= 2,
              "array.microphones must contain two or more entries")
        used = set()
        for mic in microphones:
            check(isinstance(mic, dict), "Microphone must be an object")
            channel = mic.get("channel")
            xyz = mic.get("xyz_m")
            check(type(channel) is int and 0 <= channel < 1024 and
                  channel not in used, "Invalid or duplicate microphone channel")
            used.add(channel)
            check(isinstance(xyz, list) and len(xyz) == 3 and
                  all(isinstance(v, (int, float)) and
                      not isinstance(v, bool) and math.isfinite(v)
                      for v in xyz), "Invalid microphone position")
    return recordings


def reference_direction(interval, time_s):
    if "direction_xyz" in interval:
        return tuple(interval["direction_xyz"])
    az = math.radians(reference_azimuth(interval, time_s))
    return (math.cos(az), math.sin(az), 0.0)


def reference_azimuth(interval, time_s):
    if "direction_xyz" in interval:
        x, y, _ = interval["direction_xyz"]
        return math.degrees(math.atan2(y, x))
    if "azimuth_deg" in interval:
        return float(interval["azimuth_deg"])
    fraction = (time_s - interval["start_s"]) / (
        interval["end_s"] - interval["start_s"])
    start = float(interval["azimuth_start_deg"])
    end = float(interval["azimuth_end_deg"])
    # Interpolate shortest circular path, including the +/-180 wrap.
    delta = (end - start + 180) % 360 - 180
    return (start + fraction * delta + 180) % 360 - 180


def associate_with_indices(references, observations, max_error_deg=180.0):
    """Minimum summed angular error, distinct observations per reference.

    Returns (reference-index, error) pairs; unmatched references are not
    scored as successful coverage. Small ODAS source counts make an exact
    dynamic-programming assignment inexpensive.
    """
    if not observations or not references:
        return []
    from functools import lru_cache
    errors = [[angular_error(row, angle) for row in observations]
              for angle in references]
    @lru_cache(None)
    def solve(ref_index, occupied):
        if ref_index == len(references):
            return (0, 0.0, ())
        best = solve(ref_index + 1, occupied)
        for index in range(len(observations)):
            if occupied & (1 << index) or errors[ref_index][index] > max_error_deg:
                continue
            count, cost, pairs = solve(ref_index + 1, occupied | (1 << index))
            candidate = (count + 1, cost + errors[ref_index][index],
                         ((ref_index, index, errors[ref_index][index]),) + pairs)
            if (candidate[0], -candidate[1]) > (best[0], -best[1]):
                best = candidate
        return best
    return list(solve(0, 0)[2])


def associate(references, observations):
    return [(ref, error) for ref, _, error in
            associate_with_indices(references, observations)]


def evaluate(rows, intervals, total_hops, max_association_error_deg=180.0,
             with_summary=False):
    """Angle-gated, one-to-one reference matching and recording diagnostics."""
    check(type(total_hops) is int and total_hops > 0, "Invalid hop count")
    check(math.isfinite(max_association_error_deg) and
          0 <= max_association_error_deg <= 180, "Invalid association gate")
    observed = {}
    for row in rows:
        try:
            hop = int(row["hop"])
            coords = [float(row[axis]) for axis in ("x", "y", "z")]
            if row["kind"] == "track":
                check(int(row["id"]) >= 0, "Invalid track identity")
        except (TypeError, ValueError, KeyError) as exc:
            raise ValueError("Malformed spatial trace row") from exc
        check(0 <= hop < total_hops, "Observation outside recording")
        check(row["kind"] in ("track", "proposal"), "Unknown observation kind")
        norm = sum(value * value for value in coords)
        check(all(math.isfinite(value) for value in coords) and
              math.isfinite(norm) and abs(norm - 1.0) <= 0.01,
              "Non-unit or nonfinite direction")
        observed.setdefault(hop, []).append(row)
    state = [{"errors": [], "proposals": [], "covered": 0, "hops": 0,
              "identity_switches": 0, "reacquisitions": 0}
             for _ in intervals]
    simultaneous_hops = 0
    fully_associated_hops = 0
    silent_hops = 0
    silent_hops_with_tracks = 0
    silent_track_observations = 0
    last_id = {}
    last_seen_hop = {}
    for hop in range(total_hops):
        time_s = (hop + 0.5) * HOP / FS
        active = [(index, reference_direction(interval, time_s))
                  for index, interval in enumerate(intervals)
                  if interval["start_s"] <= time_s < interval["end_s"]]
        data = observed.get(hop, [])
        tracks = [row for row in data if row["kind"] == "track"]
        if not active:
            silent_hops += 1
            if tracks:
                silent_hops_with_tracks += 1
                silent_track_observations += len(tracks)
            continue
        proposals = [row for row in data if row["kind"] == "proposal"]
        track_matches = associate_with_indices(
            [a for _, a in active], tracks, max_association_error_deg)
        proposal_matches = associate([a for _, a in active], proposals)
        for index, _ in active:
            state[index]["hops"] += 1
        for index, observation_index, error in track_matches:
            reference_index = active[index][0]
            item = state[reference_index]
            item["covered"] += 1
            item["errors"].append(error)
            track_id = int(tracks[observation_index]["id"])
            if reference_index in last_id and last_id[reference_index] != track_id:
                if last_seen_hop[reference_index] == hop - 1:
                    item["identity_switches"] += 1
                else:
                    item["reacquisitions"] += 1
            last_id[reference_index] = track_id
            last_seen_hop[reference_index] = hop
        for index, error in proposal_matches:
            state[active[index][0]]["proposals"].append(error)
        if len(active) >= 2:
            simultaneous_hops += 1
            if len(track_matches) == len(active):
                fully_associated_hops += 1
    metrics = []
    for interval, item in zip(intervals, state):
        check(item["hops"] > 0, "Interval has no evaluated hops")
        metrics.append({
            "source_id": interval.get("source_id", "default"),
            "start_s": interval["start_s"],
            "end_s": interval["end_s"],
            "reference_azimuth_start_deg": reference_azimuth(
                interval, interval["start_s"]),
            "reference_azimuth_end_deg": reference_azimuth(
                interval, interval["end_s"]),
            "track_coverage": item["covered"] / item["hops"],
            "median_track_error_deg": (
                statistics.median(item["errors"]) if item["errors"] else None),
            "p90_track_error_deg": (
                sorted(item["errors"])[math.ceil(0.9 * len(item["errors"])) - 1]
                if item["errors"] else None),
            "median_proposal_error_deg": (
                statistics.median(item["proposals"]) if item["proposals"] else None),
            "track_error_samples": len(item["errors"]),
            "adjacent_hop_id_switches": item["identity_switches"],
            "id_changes_after_gap": item["reacquisitions"],
        })
    quality = {
        "active_simultaneous_hops": simultaneous_hops,
        "fully_associated_simultaneous_hops": fully_associated_hops,
        "simultaneous_track_coverage": (
            fully_associated_hops / simultaneous_hops if simultaneous_hops else None),
        "silent_hops": silent_hops,
        "silent_hops_with_tracks": silent_hops_with_tracks,
        "silent_track_observations": silent_track_observations,
        "silent_false_positive_hop_fraction": (
            silent_hops_with_tracks / silent_hops if silent_hops else None),
        "adjacent_hop_id_switches": sum(item["identity_switches"] for item in state),
        "id_changes_after_gap": sum(item["reacquisitions"] for item in state),
        "max_association_error_deg": max_association_error_deg,
    }
    return (metrics, quality) if with_summary else metrics


def enforce_thresholds(segments, quality, options):
    """Fail closed when a requested measurement has no applicable observations."""
    for segment in segments:
        if options.max_median_error_deg is not None:
            value = segment["median_track_error_deg"]
            check(value is not None and value <= options.max_median_error_deg,
                  "DOA median acceptance failed: " + segment["source_id"])
        if options.max_p90_error_deg is not None:
            value = segment["p90_track_error_deg"]
            check(value is not None and value <= options.max_p90_error_deg,
                  "DOA P90 acceptance failed: " + segment["source_id"])
        if options.min_track_coverage is not None:
            check(segment["track_coverage"] >= options.min_track_coverage,
                  "Track coverage acceptance failed: " + segment["source_id"])
    if options.max_adjacent_id_switches is not None:
        check(quality["adjacent_hop_id_switches"] <= options.max_adjacent_id_switches,
              "Track ID-switch acceptance failed")
    if options.min_simultaneous_track_coverage is not None:
        value = quality["simultaneous_track_coverage"]
        check(value is not None and value >= options.min_simultaneous_track_coverage,
              "Simultaneous-source coverage acceptance failed")
    if options.max_silent_false_positive_fraction is not None:
        value = quality["silent_false_positive_hop_fraction"]
        check(value is not None and value <= options.max_silent_false_positive_fraction,
              "Silent false-positive acceptance failed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--max-median-error-deg", type=float)
    parser.add_argument("--max-p90-error-deg", type=float)
    parser.add_argument("--min-track-coverage", type=float)
    parser.add_argument("--max-association-error-deg", type=float, default=180.0)
    parser.add_argument("--max-adjacent-id-switches", type=int)
    parser.add_argument("--min-simultaneous-track-coverage", type=float)
    parser.add_argument("--max-silent-false-positive-fraction", type=float)
    parser.add_argument("--horizontal-sources", action="store_true")
    args = parser.parse_args()
    if args.max_median_error_deg is not None:
        check(0 <= args.max_median_error_deg <= 180, "Invalid angular threshold")
    if args.min_track_coverage is not None:
        check(math.isfinite(args.min_track_coverage) and
              0 <= args.min_track_coverage <= 1, "Invalid coverage threshold")
    if args.max_p90_error_deg is not None:
        check(math.isfinite(args.max_p90_error_deg) and
              0 <= args.max_p90_error_deg <= 180, "Invalid P90 threshold")
    check(math.isfinite(args.max_association_error_deg) and
          0 <= args.max_association_error_deg <= 180, "Invalid association gate")
    if args.max_adjacent_id_switches is not None:
        check(args.max_adjacent_id_switches >= 0, "Invalid ID-switch threshold")
    if args.min_simultaneous_track_coverage is not None:
        check(math.isfinite(args.min_simultaneous_track_coverage) and
              0 <= args.min_simultaneous_track_coverage <= 1,
              "Invalid simultaneous coverage threshold")
    if args.max_silent_false_positive_fraction is not None:
        check(math.isfinite(args.max_silent_false_positive_fraction) and
              0 <= args.max_silent_false_positive_fraction <= 1,
              "Invalid silent false-positive threshold")
    check(args.runner.is_file(), "Missing ODAS runner")
    check(args.manifest.is_file(), "Missing corpus manifest")
    corpus = validate_manifest(args.manifest)
    array = json.loads(args.manifest.read_text(encoding="utf-8")).get("array")
    args.results.mkdir(parents=True, exist_ok=True)
    report = {"schema": "libaudition.respeaker-evaluation.v1", "recordings": []}
    for rec in corpus:
        wav_path = (args.manifest.parent / rec["wav"]).resolve()
        check(wav_path.is_file(), "WAV not found: " + rec["wav"])
        check(hashlib.sha256(wav_path.read_bytes()).hexdigest() == rec["sha256"],
              "WAV checksum mismatch: " + rec["wav"])
        with wave.open(str(wav_path)) as stream:
            check((array is not None or stream.getnchannels() == 6) and stream.getframerate() == FS
                  and stream.getsampwidth() == 2 and
                  stream.getcomptype() == "NONE", "Expected 6-channel PCM16 16kHz")
            frames = stream.getnframes()
            if array is not None:
                check(all(mic["channel"] < stream.getnchannels()
                          for mic in array["microphones"]),
                      "Microphone channel outside WAV channel count")
        check(frames > 0 and frames % HOP == 0, "WAV must contain full ODAS hops")
        for interval in rec["intervals"]:
            check(interval["end_s"] <= frames / FS,
                  "Ground truth extends beyond WAV duration")
        dest = args.results / rec["id"]
        dest.mkdir(parents=True, exist_ok=True)
        cmd = [str(args.runner), str(wav_path), str(dest)]
        if args.horizontal_sources:
            cmd.append("--horizontal-sources")
        if array is not None:
            for mic in array["microphones"]:
                cmd.extend(["--mic", ":".join([
                    str(mic["channel"]), *(str(v) for v in mic["xyz_m"])
                ])])
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
        segments, quality = evaluate(
            rows, rec["intervals"], summary["hops"],
            args.max_association_error_deg, with_summary=True)
        item = {"id": rec["id"], "wav_sha256": rec["sha256"],
                "segments": segments, "summary": summary, "quality": quality,
                "adjacent_hop_id_switches": quality["adjacent_hop_id_switches"],
                "id_changes_after_gap": quality["id_changes_after_gap"]}
        report["recordings"].append(item)
        (dest / "metrics.json").write_text(
            json.dumps(item, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        enforce_thresholds(segments, quality, args)
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
