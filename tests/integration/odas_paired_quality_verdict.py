#!/usr/bin/env python3
"""Fail-closed paired ODAS quality verdict, distinct from pipeline/CI integrity."""
import argparse
import json
import math
from pathlib import Path

CASES = {"t02_dev_single": 1, "t03_eval_two": 2, "t07_eval_seven": 7}
VARIANTS = ("upstream", "dev", "feature")
METRICS = ("matched_voice_source_fraction", "matched_p90_azimuth_error_deg",
           "max_simultaneous_tracks", "adjacent_matched_id_switches")


def evaluate(root):
    runs = {}
    for name in VARIANTS:
        p = Path(root) / ("odas-ab-" + name) / "ab_result.json"
        if not p.is_file():
            raise ValueError("missing real-run artifact: " + str(p))
        runs[name] = json.loads(p.read_text(encoding="utf-8"))
        if runs[name].get("variant") != name or not runs[name].get("odas_sha"):
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
                if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v):
                    raise ValueError("invalid native metric: " + name + "/" + case + "/" + key)
            if not 0 <= metrics["matched_voice_source_fraction"] <= 1:
                raise ValueError("invalid coverage")
            if metrics["max_simultaneous_tracks"] < 0 or metrics["adjacent_matched_id_switches"] < 0:
                raise ValueError("invalid tracking count")
            records[name] = {key: metrics[key] for key in METRICS}
        if len(fingerprints) != 1 or any(not x for x in next(iter(fingerprints))):
            raise ValueError("A/B input/oracle mismatch: " + case)
        base, feature = records["upstream"], records["feature"]
        report["cases"][case] = {
            "expected_simultaneous_sources": expected, "variants": records,
            "feature_minus_upstream": {
                "matched_voice_source_fraction": round(feature["matched_voice_source_fraction"] - base["matched_voice_source_fraction"], 6),
                "matched_p90_azimuth_error_deg": round(feature["matched_p90_azimuth_error_deg"] - base["matched_p90_azimuth_error_deg"], 4),
                "max_simultaneous_tracks": feature["max_simultaneous_tracks"] - base["max_simultaneous_tracks"]},
            "feature_capacity_deficit": max(0, expected - feature["max_simultaneous_tracks"]),
            "status": "CAPACITY_DEFICIT" if feature["max_simultaneous_tracks"] < expected else "CAPACITY_ONLY_NOT_IDF1_OR_SSS_PASS"}
    report["quality_blockers"] = [
        "T03: fewer than two simultaneous tracks" if report["cases"]["t03_eval_two"]["feature_capacity_deficit"] else None,
        "T07: fewer than seven simultaneous tracks" if report["cases"]["t07_eval_seven"]["feature_capacity_deficit"] else None,
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
