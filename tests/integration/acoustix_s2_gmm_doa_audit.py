#!/usr/bin/env python3
"""One-to-one 3-D geometric check of *actual* active ODAS tracks vs Acoustix T03.

Distinct track IDs and distinct oracle human sources are required for a
two-speaker geometric hit. This does NOT establish acoustic isolation,
speaker identity, intelligibility or ASR success. Contracts are separate
from synthetic unit-test fixtures.
"""
from collections import defaultdict
import csv
import math
from pathlib import Path
import statistics


class GeometryError(ValueError):
    pass


def oracle_unit_vector(source):
    az, el = math.radians(source["bearing_deg"]), math.radians(
        source.get("elevation_deg", 0.0))
    return (math.cos(az) * math.cos(el), math.sin(az) * math.cos(el),
            math.sin(el))


def vector_angle_deg(oracle, track):
    xyz = tuple(float(track[key]) for key in ("x", "y", "z"))
    if not all(math.isfinite(v) for v in xyz):
        raise GeometryError("nonfinite ODAS active track direction")
    norm = math.sqrt(sum(v * v for v in xyz))
    if not 0.98 <= norm <= 1.02:
        raise GeometryError("invalid nonunit ODAS track vector")
    expected = oracle_unit_vector(oracle)
    dot = sum(a * b for a, b in zip(expected, xyz)) / norm
    return math.degrees(math.acos(max(-1.0, min(1.0, dot))))


def read_native_tracks(path, hop_limit=1875):
    by_hop = defaultdict(list)
    with Path(path).open(newline="", encoding="utf-8") as src:
        reader = csv.DictReader(src)
        if reader.fieldnames != ["hop", "kind", "id", "x", "y", "z", "activity"]:
            raise GeometryError("unexpected native ODAS observations.csv header")
        for record in reader:
            if record["kind"] not in ("proposal", "track"):
                raise GeometryError("unexpected ODAS observation kind")
            try:
                hop = int(record["hop"])
            except ValueError as exc:
                raise GeometryError("noninteger ODAS hop") from exc
            if not 0 <= hop < hop_limit:
                raise GeometryError("ODAS observations outside PCM clock")
            if record["kind"] != "track":
                continue
            try:
                track_id = int(record["id"])
                activity = float(record["activity"])
            except ValueError as exc:
                raise GeometryError("bad native track ID/activity") from exc
            if track_id <= 0 or not math.isfinite(activity) or not 0 <= activity <= 1.00001:
                raise GeometryError("invalid ODAS track identity/activity")
            if any(t["track_id"] == track_id for t in by_hop[hop]):
                raise GeometryError("duplicate ODAS track ID at same hop")
            record["track_id"] = track_id
            by_hop[hop].append(record)
            if len(by_hop[hop]) > 4:
                raise GeometryError("ODAS track capacity exceeded")
    return by_hop


def measure_t03_oracle_pairs(oracle, native_tracks, tolerance_25=25.0,
                             tolerance_45=45.0):
    if not oracle or len(oracle) > 1875:
        raise GeometryError("missing or oversized true Acoustix oracle")
    if not 0 < tolerance_25 <= tolerance_45 <= 180:
        raise GeometryError("invalid angular tolerance")
    hops_with_two_oracle = 0
    hops_with_two_active = 0
    distinct_pairs_25 = 0
    distinct_pairs_45 = 0
    max_error_by_hop = []
    pair_track_ids = defaultdict(int)
    for i, point in enumerate(oracle):
        if point["hop"] != i:
            raise GeometryError("oracle hop discontinuity")
        people = sorted((p for p in point["sources"] if p.get("kind") == "voice"),
                        key=lambda p: p["id"])
        if len(people) != 2:
            continue
        if people[0]["id"] == people[1]["id"]:
            raise GeometryError("duplicate oracle voice ID")
        hops_with_two_oracle += 1
        tracks = native_tracks.get(i, [])
        if len(tracks) < 2:
            continue
        hops_with_two_active += 1
        viable_pairs = []
        for first in tracks:
            for second in tracks:
                if first is second:
                    continue
                err1 = vector_angle_deg(people[0], first)
                err2 = vector_angle_deg(people[1], second)
                viable_pairs.append((max(err1, err2), err1 + err2,
                                     first["track_id"], second["track_id"]))
        if not viable_pairs:
            raise GeometryError("active-track pairing unexpectedly empty")
        worst_error, _, id1, id2 = min(viable_pairs)
        max_error_by_hop.append(worst_error)
        pair_track_ids[tuple(sorted((id1, id2)))] += 1
        distinct_pairs_25 += worst_error <= tolerance_25
        distinct_pairs_45 += worst_error <= tolerance_45
    if hops_with_two_oracle == 0:
        raise GeometryError("no genuine two-voice overlap in T03 oracle")
    return {
        "scope": "3-D geometric track/oracle matching only; NOT audio separation, speaker identification, or SI-SDRi",
        "oracle_exactly_two_voice_hops": hops_with_two_oracle,
        "two_or_more_active_tracks_in_overlap_hops": hops_with_two_active,
        "two_distinct_track_to_voice_matches_within_25deg_hops": distinct_pairs_25,
        "two_distinct_track_to_voice_matches_within_45deg_hops": distinct_pairs_45,
        "two_active_but_no_distinct_two_voice_match_45deg_hops":
            hops_with_two_active - distinct_pairs_45,
        "best_pair_worst_error_median_deg":
            round(statistics.median(max_error_by_hop), 6)
            if max_error_by_hop else None,
        "best_pair_worst_error_min_deg":
            round(min(max_error_by_hop), 6) if max_error_by_hop else None,
        "active_track_id_pairs_by_hops": [
            {"ids": list(pair), "hops": count}
            for pair, count in sorted(pair_track_ids.items(),
                                      key=lambda item: (-item[1], item[0]))
        ],
    }
