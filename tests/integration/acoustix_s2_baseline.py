"""Compare unchanged real-speech ODAS replay against frozen observed baseline.

A regression check is NOT a quality-acceptance test. It must not treat
one SST track as multiple separated speakers or a correct transcript.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def compare(baseline, report):
    if baseline.get("schema") != "libaudition.acoustix-s2-odas-baseline.v1":
        raise ValueError("unsupported frozen baseline schema")
    if report.get("schema") != "libaudition.acoustix-s2-real-odas-diagnostics.v1":
        raise ValueError("unsupported replay diagnostics schema")
    if not isinstance(baseline.get("cases"), list) or not isinstance(report.get("cases"), list):
        raise ValueError("cases must be arrays")
    expected = {case["case"]: case for case in baseline["cases"]}
    observed = {case["case"]: case for case in report["cases"]}
    if len(expected) != len(baseline["cases"]) or len(observed) != len(report["cases"]):
        raise ValueError("duplicate case identifiers")
    if expected.keys() != observed.keys():
        raise ValueError("missing or extra replay cases")
    failures = []
    cases = []
    for name in sorted(expected):
        old, now = expected[name], observed[name]
        if old["input_sha256"] != now["input_sha256"]:
            raise ValueError("input WAV hash mismatch: " + name)
        if old["oracle_voice_source_hops"] != now["oracle_voice_source_hops"]:
            raise ValueError("oracle denominator changed: " + name)
        denominator = old["oracle_voice_source_hops"]
        if not isinstance(denominator, int) or denominator <= 0:
            raise ValueError("invalid oracle denominator: " + name)
        # Reject malformed observations before applying one-sided regression gates.
        # Otherwise negative drops/switches, negative matches or impossible
        # matches > oracle could silently pass as improvements.
        for key in ("matched_voice_source_hops", "max_simultaneous_tracks",
                    "adjacent_id_switches", "recorder_drops"):
            value = now.get(key)
            if type(value) is not int or value < 0:
                raise ValueError("invalid nonnegative integer " + key + ": " + name)
        if now["matched_voice_source_hops"] > denominator:
            raise ValueError("matched source hops exceed oracle: " + name)
        if not isinstance(now.get("passed_pipeline_integrity"), bool):
            raise ValueError("missing pipeline integrity result: " + name)
        # Fixed-input comparison: 5% relative tolerance on matched source-hop
        # count avoids failing on small scheduling variation; do not use as a
        # threshold to claim the algorithm is accurate.
        min_matches = (old["matched_voice_source_hops"] * 95 + 99) // 100
        checks = {
            "no_recorder_drops": now["recorder_drops"] == 0,
            "pipeline_integrity": now["passed_pipeline_integrity"],
            "source_hop_recall_not_regressed": now["matched_voice_source_hops"] >= min_matches,
            "id_switches_not_increased":
                now["adjacent_id_switches"] <= old["adjacent_id_switches"],
        }
        failures.extend(name + ":" + key for key, passed in checks.items() if not passed)
        cases.append({
            "case": name,
            "baseline_matched_source_hops": old["matched_voice_source_hops"],
            "observed_matched_source_hops": now["matched_voice_source_hops"],
            "minimum_matched_source_hops": min_matches,
            "oracle_voice_source_hops": denominator,
            "baseline_max_simultaneous_tracks": old["max_simultaneous_tracks"],
            "observed_max_simultaneous_tracks": now["max_simultaneous_tracks"],
            "expected_speakers": {"t02_dev_single": 1, "t03_eval_two": 2,
                                  "t07_eval_seven": 7}[name],
            "checks": checks,
        })
    return {
        "schema": "libaudition.acoustix-s2-odas-baseline-comparison.v1",
        "regression_passed": not failures,
        "regressions": failures,
        "cases": cases,
        "separation_validated": False,
        "asr_validated": False,
        "physical_room_validated": False,
        "note": "Frozen-input SST/DOA regression only; incomplete source coverage remains BLOCKED.",
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = compare(json.loads(args.baseline.read_text()),
                     json.loads(args.report.read_text()))
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["regression_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
