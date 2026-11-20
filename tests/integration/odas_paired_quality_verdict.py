#!/usr/bin/env python3
"""Fail-closed paired ODAS quality verdict, distinct from pipeline/CI integrity."""
import argparse
import json
import math
from pathlib import Path

CASES = {"t02_dev_single": 1, "t03_eval_two": 2, "t07_eval_seven": 7}
VARIANTS = ("upstream", "dev", "feature")
# Frozen revisions for this particular paired experiment; reject mislabeled artifacts.
EXPECTED_SHAS = {
    "upstream": "004cb824c6073771cb23cf9139f01e9d8581025b",
    "dev": "4c8e3bfcb56124ee340477a2443ea2ebe11ee712",
    "feature": "ecbf6af806536415692bd4bad305be8cd9f6bfc3",
}
# Diagnostic tolerances, NOT product acceptance thresholds.
AZIMUTH_P90_REGRESSION_TOLERANCE_DEG = 1.0
COVERAGE_REGRESSION_TOLERANCE = 0.01
METRICS = ("matched_voice_source_fraction", "matched_p90_azimuth_error_deg",
           "max_simultaneous_tracks", "adjacent_matched_id_switches",
           "max_tracks_during_multi_voice", "max_unique_oracle_matches_during_multi_voice",
           "multi_voice_hops", "fully_matched_multi_voice_hops",
           "fully_matched_multi_voice_fraction",
           "unmatched_track_observations_during_voice",
           "max_unmatched_tracks_during_voice")


def evaluate(root):
    runs = {}
    for name in VARIANTS:
        p = Path(root) / ("odas-ab-" + name) / "ab_result.json"
        if not p.is_file():
            raise ValueError("missing real-run artifact: " + str(p))
        runs[name] = json.loads(p.read_text(encoding="utf-8"))
        if runs[name].get("variant") != name or runs[name].get("odas_sha") != EXPECTED_SHAS[name]:
            raise ValueError("missing/incorrect revision provenance: " + name)
        if set(runs[name].get("cases", {})) != set(CASES):
            raise ValueError("incomplete case matrix: " + name)
    report = {"schema": "libaudition.odas-paired-quality.v1", "cases": {},
              "quality_status": "BLOCKED"}
    for case, expected in CASES.items():
        fingerprints = set()
        records = {}
        for name in VARIANTS:
            data = runs[name]["cases"][case]
            fingerprints.add((data.get("wav_sha256"), data.get("oracle_sha256")))
            metrics = data.get("metrics", {})
            for key in METRICS:
                v = metrics.get(key)
                if key == "fully_matched_multi_voice_fraction" and metrics.get("multi_voice_hops") == 0 and v is None:
                    continue  # Undefined (not zero) when no simultaneous speech exists.
                if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v):
                    raise ValueError("invalid native metric: " + name + "/" + case + "/" + key)
            if any(metrics[key] < 0 or int(metrics[key]) != metrics[key] for key in ("max_simultaneous_tracks", "adjacent_matched_id_switches", "unmatched_track_observations_during_voice", "max_unmatched_tracks_during_voice", "max_tracks_during_multi_voice", "max_unique_oracle_matches_during_multi_voice", "multi_voice_hops", "fully_matched_multi_voice_hops")):
                raise ValueError("invalid tracking count")
            if expected > 1 and metrics["multi_voice_hops"] == 0:
                raise ValueError("missing oracle overlap for multi-speaker case: " + case)
            if metrics["multi_voice_hops"] > 0 and not math.isclose(
                    metrics["fully_matched_multi_voice_fraction"],
                    metrics["fully_matched_multi_voice_hops"] / metrics["multi_voice_hops"],
                    abs_tol=1e-6):
                raise ValueError("inconsistent overlap fraction: " + name + "/" + case)
            if not 0 <= metrics["matched_voice_source_fraction"] <= 1:
                raise ValueError("invalid coverage")
            if metrics["max_unmatched_tracks_during_voice"] > metrics["max_simultaneous_tracks"]:
                raise ValueError("unmatched tracks exceed track capacity")
            if metrics["multi_voice_hops"] == 0 and metrics["fully_matched_multi_voice_hops"] != 0:
                raise ValueError("overlap matches without overlap")
            if metrics["multi_voice_hops"] > 0 and not 0 <= metrics["fully_matched_multi_voice_fraction"] <= 1:
                raise ValueError("invalid full-overlap coverage")
            if not 0 <= metrics["fully_matched_multi_voice_hops"] <= metrics["multi_voice_hops"]:
                raise ValueError("invalid full-overlap counts")
            if not 0 <= metrics["max_unique_oracle_matches_during_multi_voice"] <= metrics["max_tracks_during_multi_voice"]:
                raise ValueError("invalid one-to-one overlap capacity")
            # Oracle cardinality is a hard bound, independent of raw SST
            # proposals. A malformed replay must not fabricate extra matches.
            if metrics["max_unique_oracle_matches_during_multi_voice"] > expected:
                raise ValueError("oracle matches exceed source cardinality: " + name + "/" + case)
            if metrics["max_tracks_during_multi_voice"] > metrics["max_simultaneous_tracks"]:
                raise ValueError("overlap track count exceeds peak capacity: " + name + "/" + case)
            if (metrics["fully_matched_multi_voice_hops"] > 0 and
                    metrics["max_unique_oracle_matches_during_multi_voice"] < expected):
                raise ValueError("full overlap without complete oracle matches: " + name + "/" + case)
            records[name] = {key: metrics[key] for key in METRICS}
        if len(fingerprints) != 1 or any(not x for x in next(iter(fingerprints))):
            raise ValueError("A/B input/oracle mismatch: " + case)
        base, dev, feature = records["upstream"], records["dev"], records["feature"]
        regression_flags = []
        for label, reference in (("upstream", base), ("dev", dev)):
            if feature["matched_voice_source_fraction"] < reference["matched_voice_source_fraction"] - COVERAGE_REGRESSION_TOLERANCE:
                regression_flags.append("coverage worse than " + label)
            if feature["matched_p90_azimuth_error_deg"] > reference["matched_p90_azimuth_error_deg"] + AZIMUTH_P90_REGRESSION_TOLERANCE_DEG:
                regression_flags.append("P90 azimuth worse than " + label)
            if expected == 1 and feature["unmatched_track_observations_during_voice"] > reference["unmatched_track_observations_during_voice"]:
                regression_flags.append("unmatched single-voice tracks worse than " + label)
            if expected > 1 and feature["fully_matched_multi_voice_fraction"] < reference["fully_matched_multi_voice_fraction"] - COVERAGE_REGRESSION_TOLERANCE:
                regression_flags.append("full overlap 1:1 matching worse than " + label)
            if feature["max_unique_oracle_matches_during_multi_voice"] < reference["max_unique_oracle_matches_during_multi_voice"]:
                regression_flags.append("1:1 overlap capacity worse than " + label)
            # Raw SST track count is not a quality capacity metric: fewer
            # tracks may mean ghost suppression. Judge real-source capacity
            # exclusively by simultaneous 1:1 oracle matches above.
            # A directionally plausible track is not necessarily a real speaker.
            # Reject ghost observations even when geometric coverage improves.
            if feature["unmatched_track_observations_during_voice"] > reference["unmatched_track_observations_during_voice"]:
                regression_flags.append("unmatched track observations worse than " + label)
            if feature["adjacent_matched_id_switches"] > reference["adjacent_matched_id_switches"]:
                regression_flags.append("matched ID switches worse than " + label)
        report["cases"][case] = {
            "expected_simultaneous_sources": expected, "variants": records,
            "regression_flags": regression_flags,
            "feature_minus_upstream": {
                "matched_voice_source_fraction": round(feature["matched_voice_source_fraction"] - base["matched_voice_source_fraction"], 6),
                "matched_p90_azimuth_error_deg": round(feature["matched_p90_azimuth_error_deg"] - base["matched_p90_azimuth_error_deg"], 4),
                "max_simultaneous_tracks": feature["max_simultaneous_tracks"] - base["max_simultaneous_tracks"]},
            "feature_capacity_deficit": max(0, expected - feature["max_unique_oracle_matches_during_multi_voice"]) if expected > 1 else None,
            "single_voice_ghost_track_observations": feature["unmatched_track_observations_during_voice"] if expected == 1 else None,
            "status": "NOT_APPLICABLE_SINGLE_SPEAKER" if expected == 1 else ("TRACK_CAPACITY_DEFICIT" if feature["max_tracks_during_multi_voice"] < expected
                       else "ORACLE_MATCH_DEFICIT")
            if feature["max_unique_oracle_matches_during_multi_voice"] < expected
            else "OVERLAP_MATCH_ONLY_NOT_IDF1_OR_SSS_PASS"}
    report["quality_blockers"] = [
        case + ": " + flag
        for case, record in report["cases"].items()
        for flag in record["regression_flags"]
    ] + [
        "T03: fewer than two simultaneous 1:1 oracle-matched tracks" if report["cases"]["t03_eval_two"]["feature_capacity_deficit"] else None,
        "T07: fewer than seven simultaneous 1:1 oracle-matched tracks" if report["cases"]["t07_eval_seven"]["feature_capacity_deficit"] else None,
        "No 1:1 source IDF1/false-track/latency acceptance in this A/B",
        "No separated source PCM, SI-SDRi, SIRi, leakage or per-speaker WER in this A/B",
    ]
    report["quality_blockers"] = [x for x in report["quality_blockers"] if x]
    return report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    report = evaluate(args.results)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"quality_status": report["quality_status"],
                      "quality_blockers": report["quality_blockers"]}, indent=2))


if __name__ == "__main__":
    main()
