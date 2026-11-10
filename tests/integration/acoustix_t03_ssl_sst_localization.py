#!/usr/bin/env python3
"""Localize T03 two-voice failures to SSL proposals or SST active tracks.

Consumes the *same* native ODAS observations.csv and hop-indexed oracle.
This is geometric coverage only, not source separation or speaker identity.
"""
import csv
import json
import math
from pathlib import Path
import argparse


def angle(voice, candidate):
    az = math.radians(float(voice["bearing_deg"]))
    el = math.radians(float(voice.get("elevation_deg", 0)))
    target = (math.cos(az)*math.cos(el), math.sin(az)*math.cos(el), math.sin(el))
    xyz = tuple(float(candidate[k]) for k in ("x", "y", "z"))
    if not all(math.isfinite(v) for v in xyz):
        raise ValueError("non-finite native direction")
    norm = math.sqrt(sum(v*v for v in xyz))
    if not 0.98 <= norm <= 1.02:
        raise ValueError("non-unit native direction")
    dot = sum(x*y for x,y in zip(target,xyz))/norm
    return math.degrees(math.acos(max(-1., min(1., dot))))


def load_observations(path, hop_limit):
    observations = {}
    with Path(path).open(newline="", encoding="utf-8") as src:
        reader = csv.DictReader(src)
        if reader.fieldnames != ["hop","kind","id","x","y","z","activity"]:
            raise ValueError("invalid native observations.csv header")
        for row in reader:
            hop = int(row["hop"])
            if not 0 <= hop < hop_limit or row["kind"] not in ("proposal","track"):
                raise ValueError("invalid native hop or kind")
            if not math.isfinite(float(row["activity"])):
                raise ValueError("invalid native activity")
            observations.setdefault(hop, {"proposal":[],"track":[]})[row["kind"]].append(row)
    for hop, groups in observations.items():
        # SSL proposal IDs are candidate slot labels, not persistent identities.\n        # Native ODAS may legitimately reuse a slot label within a hop.\n        # SST active track IDs, by contrast, must be unique per hop.\n        ids = [row["id"] for row in groups["track"]]\n        if len(ids) != len(set(ids)):\n            raise ValueError(f"duplicate native track ID at hop {hop}")
    return observations


def pair_hits(voices, candidates, threshold):
    if len(voices) != 2:
        raise ValueError("two distinct oracle voices required")
    if voices[0]["id"] == voices[1]["id"]:
        raise ValueError("duplicate oracle voice")
    if len(candidates) < 2:
        return False
    return any(i != j and angle(voices[0], a) <= threshold and
               angle(voices[1], b) <= threshold
               for i,a in enumerate(candidates) for j,b in enumerate(candidates))


def diagnose(oracle, observations, threshold=45.):
    if not 0 < threshold <= 180:
        raise ValueError("invalid threshold")
    counts = dict(overlap_hops=0, ssl_two_matches=0, sst_two_matches=0,
                  ssl_miss=0, ssl_hit_sst_miss=0)
    for hop, row in enumerate(oracle):
        if row["hop"] != hop:
            raise ValueError("non-contiguous oracle hops")
        voices = sorted((s for s in row["sources"] if s.get("kind")=="voice"),
                        key=lambda s:s["id"])
        if len(voices) != 2:
            continue
        counts["overlap_hops"] += 1
        native = observations.get(hop, {"proposal":[],"track":[]})
        ssl = pair_hits(voices, native["proposal"], threshold)
        sst = pair_hits(voices, native["track"], threshold)
        counts["ssl_two_matches"] += int(ssl)
        counts["sst_two_matches"] += int(sst)
        counts["ssl_miss"] += int(not ssl)
        counts["ssl_hit_sst_miss"] += int(ssl and not sst)
    if not counts["overlap_hops"]:
        raise ValueError("oracle has no two-voice overlap")
    return {"schema":"libaudition.t03_ssl_sst_failure_localization.v1",
            "threshold_deg":threshold, **counts,
            "interpretation":"geometric proposal/track coverage only; SSS unmeasured"}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--oracle",type=Path,required=True)
    p.add_argument("--observations",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--threshold-deg",type=float,default=45.)
    a=p.parse_args()
    oracle=json.loads(a.oracle.read_text())
    if isinstance(oracle,dict):
        oracle=oracle["ground_truth"]
    result=diagnose(oracle,load_observations(a.observations,len(oracle)),a.threshold_deg)
    a.output.write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")


if __name__=="__main__":
    main()
