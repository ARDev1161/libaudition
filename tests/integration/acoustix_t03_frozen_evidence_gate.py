#!/usr/bin/env python3
"""Fail closed on provenance and known T03 structural failure, not quality PASS.

This frozen reference is a characterization baseline, not an acceptance goal.
"""
import argparse
import json
from pathlib import Path

EXPECTED = {
    "overlap_hops": 243,
    "ssl_two_matches": 214,
    "sst_two_matches": 0,
    "ssl_hit_sst_miss": 214,
    "ssl_and_sst_miss": 29,
    "sst_tracks_in_overlap": 243,
}


def verify(report):
    if report.get("schema") != "libaudition.t03_ssl_sst_failure_localization.v1":
        raise ValueError("unsupported native localization schema")
    if report.get("threshold_deg") != 45.0:
        raise ValueError("changed geometric tolerance")
    for key, expected in EXPECTED.items():
        if type(report.get(key)) is not int or report[key] != expected:
            raise ValueError(f"frozen native T03 {key} changed: {report.get(key)!r} != {expected}")
    hops = report.get("hop_attribution")
    if not isinstance(hops, list) or len(hops) != 243:
        raise ValueError("missing per-hop evidence")
    if len({h.get("hop") for h in hops}) != 243:
        raise ValueError("duplicate native overlap hop")
    if any(h.get("sst_tracks") != 1 for h in hops):
        raise ValueError("frozen T03 expected exactly one native SST track in each overlap hop")
    if sum(h.get("ssl_candidates", -1) for h in hops) != report.get("ssl_proposals_in_overlap"):
        raise ValueError("native proposal accounting mismatch")
    if sum(h["sst_tracks"] for h in hops) != report["sst_tracks_in_overlap"]:
        raise ValueError("native SST track accounting mismatch")
    return {"baseline_verified": True, "status": "KNOWN_SST_CAPACITY_FAILURE",
            "overlap_hops": 243, "ssl_two_matches": 214, "sst_two_matches": 0,
            "meaning": "native real-speech structural failure; NOT a successful source separation test"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(json.loads(args.report.read_text())), indent=2))


if __name__ == "__main__":
    main()
