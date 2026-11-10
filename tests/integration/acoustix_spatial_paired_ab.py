#!/usr/bin/env python3
"""Compare frozen SSL/SST/SSS measurements without claiming unmeasured quality.

Inputs are *measured* per-run JSON manifests, never synthesized scores.
Both runs must contain the same WAV hash, scene/oracle hash and configuration
hash. A missing quality metric is reported as unmeasured, never as PASS.
"""
import argparse
import json
import math
from pathlib import Path

METRICS = {
    "ssl": ("candidate_recall", "candidate_precision", "azimuth_median_deg",
            "azimuth_p90_deg", "false_candidates_per_second"),
    "sst": ("source_recall", "source_precision", "idf1", "id_switches",
            "false_tracks", "birth_latency_ms", "retirement_latency_ms"),
    "sss": ("si_sdri_db", "siri_db", "interferer_leakage_db",
            "target_retention_db"),
}
HIGHER_BETTER = {"candidate_recall", "candidate_precision", "source_recall",
                 "source_precision", "idf1", "si_sdri_db", "siri_db",
                 "target_retention_db"}
REQUIRED_IDENTITY = ("input_wav_sha256", "oracle_sha256", "configuration_sha256",
                     "case_id", "input_frames", "sample_rate_hz")


def compare(a, b):
    for key in REQUIRED_IDENTITY:
        if key not in a or key not in b or a[key] != b[key]:
            raise ValueError("unpaired A/B: missing or different " + key)
    result = {"case_id": a["case_id"], "identity": {k: a[k] for k in REQUIRED_IDENTITY},
              "metrics": {}, "complete": True, "regressions": []}
    for group, names in METRICS.items():
        values = {}
        for name in names:
            x = a.get(group, {}).get(name)
            y = b.get(group, {}).get(name)
            if x is None or y is None:
                values[name] = {"status": "UNMEASURED"}
                result["complete"] = False
                continue
            if isinstance(x, bool) or isinstance(y, bool) or not all(
                    isinstance(z, (int, float)) and math.isfinite(z) for z in (x, y)):
                raise ValueError("non-finite/non-numeric metric: " + group + "." + name)
            delta = y - x
            improvement = delta if name in HIGHER_BETTER else -delta
            status = "IMPROVED" if improvement > 0 else ("REGRESSED" if improvement < 0 else "UNCHANGED")
            values[name] = {"before": x, "after": y, "delta": delta, "status": status}
            if status == "REGRESSED":
                result["regressions"].append(group + "." + name)
        result["metrics"][group] = values
    result["status"] = ("INCOMPLETE" if not result["complete"] else
                        "REGRESSIONS" if result["regressions"] else "NO_REGRESSIONS")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = compare(json.loads(args.before.read_text()), json.loads(args.after.read_text()))
    serialized = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(serialized)
    else:
        print(serialized, end="")
    # This is a reporting tool: INCOMPLETE is not success; quality gates
    # must be separately reviewed and frozen on held-out measurements.
    return 2 if report["status"] == "INCOMPLETE" else 1 if report["regressions"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
