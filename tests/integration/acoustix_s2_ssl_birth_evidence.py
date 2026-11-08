#!/usr/bin/env python3
"""Real Acoustix/ODAS SSL evidence, NOT isolated speech or speaker identity.

Find two distinct SSL directions within a three-dimensional oracle cone,
then inspect the ODAS birth posterior and association evidence associated
with those candidates. No fabricated PCM, tracks or oracle labels.
"""
import argparse
import hashlib
import json
import math
import statistics
from pathlib import Path


def oracle_vector(source):
    az = math.radians(source["bearing_deg"])
    el = math.radians(source["elevation_deg"])
    return (math.cos(el) * math.cos(az), math.cos(el) * math.sin(az),
            math.sin(el))


def angle_deg(a, b):
    return math.degrees(math.acos(max(-1.0, min(1.0,
        sum(x * y for x, y in zip(a, b))))))


def choose_two(sources, candidates, tolerance):
    """Minimum TOTAL 3D angular error, distinct candidate indices only."""
    if len(sources) != 2 or not 0.0 < tolerance <= 180.0:
        raise ValueError("expected two sources and a valid cone")
    oracle = [oracle_vector(s) for s in sources]
    directions = []
    for i, c in enumerate(candidates):
        xyz = [c[k] for k in ("x", "y", "z")]
        if any(type(v) not in (int, float) or not math.isfinite(v)
               for v in xyz):
            raise ValueError("nonfinite SSL candidate direction")
        norm = math.sqrt(sum(v * v for v in xyz))
        if norm < 1.0e-5:
            continue  # no direction
        if abs(norm - 1.0) > 0.01:
            raise ValueError("nonunit SSL candidate direction")
        directions.append((i, tuple(v / norm for v in xyz)))
    eligible = []
    for i, first in directions:
        error1 = angle_deg(first, oracle[0])
        if error1 > tolerance:
            continue
        for j, second in directions:
            if i == j:
                continue
            error2 = angle_deg(second, oracle[1])
            if error2 <= tolerance:
                eligible.append((error1 + error2, max(error1, error2),
                                 i, j))
    if not eligible:
        return None
    _, _, i, j = min(eligible)
    return i, j


def distribution(values):
    if not values:
        return None
    values = sorted(values)
    return {"min": round(values[0], 8),
            "median": round(statistics.median(values), 8),
            "p90": round(values[round((len(values) - 1) * 0.9)], 8),
            "max": round(values[-1], 8)}


def streaks(matches):
    lengths = []
    current = 0
    for yes in list(matches) + [False]:
        if yes:
            current += 1
        elif current:
            lengths.append(current)
            current = 0
    return {"runs": len(lengths),
            "median_run_hops": statistics.median(lengths) if lengths else 0,
            "longest_hops": max(lengths, default=0),
            "runs_at_least_5_hops": sum(x >= 5 for x in lengths)}


def analyse(rows, ground_truth, tolerance=25.0):
    if not ground_truth or len(ground_truth) > len(rows):
        raise ValueError("missing or oversized Acoustix oracle")
    matched = []
    probs = [[], []]
    strengths = [[], []]
    assigned = [[], []]
    above = [0, 0]
    by_sep = {"<30": {"hops": 0, "paired": 0},
              "30-60": {"hops": 0, "paired": 0},
              ">=60": {"hops": 0, "paired": 0}}
    total = 0
    any_two_above = 0
    matched_two_above = 0
    two_active = 0
    distances = []
    for index, record in enumerate(rows):
        if (record["hop_index"] != index or
                record["sample_offset"] != index * 128 or
                record["odas_timestamp"] != index + 1):
            raise ValueError("ODAS timestamp or sample offset changed")
        if index >= len(ground_truth):
            continue
        oracle_hop = ground_truth[index]
        if oracle_hop["hop"] != index:
            raise ValueError("Acoustix oracle timeline discontinuity")
        sources = sorted((s for s in oracle_hop["sources"]
                          if s.get("kind") == "voice"), key=lambda s: s["id"])
        if len(sources) != 2:
            continue
        if sources[0]["id"] == sources[1]["id"]:
            raise ValueError("duplicate voice in oracle")
        total += 1
        sep = angle_deg(oracle_vector(sources[0]), oracle_vector(sources[1]))
        distances.append(sep)
        key = "<30" if sep < 30 else "30-60" if sep < 60 else ">=60"
        by_sep[key]["hops"] += 1
        candidates = record["ssl_candidates"]
        theta = record["theta_new"]
        if type(theta) not in (int, float) or not math.isfinite(theta) or not 0 <= theta <= 1:
            raise ValueError("invalid SST birth threshold")
        for c in candidates:
            value = c["birth_posterior"]
            if (type(value) not in (int, float) or
                    not math.isfinite(value) or not 0 <= value <= 1):
                raise ValueError("invalid birth posterior")
        any_two_above += sum(c["birth_posterior"] > theta for c in candidates) >= 2
        two_active += sum(t["state"] == "A" for t in record["tracks_after"]) >= 2
        chosen = choose_two(sources, candidates, tolerance)
        matched.append(chosen is not None)
        if chosen is None:
            continue
        by_sep[key]["paired"] += 1
        selected = [candidates[j] for j in chosen]
        matched_two_above += all(c["birth_posterior"] > theta for c in selected)
        for voice, candidate in enumerate(selected):
            strength = candidate["strength"]
            if (type(strength) not in (int, float) or
                    not math.isfinite(strength) or strength < 0):
                raise ValueError("invalid SSL candidate strength")
            association = [p["posterior"] for p in candidate["track_posteriors"]]
            if any(type(p) not in (int, float) or not math.isfinite(p) or not 0 <= p <= 1
                   for p in association):
                raise ValueError("invalid track association marginal")
            prob = candidate["birth_posterior"]
            probs[voice].append(prob)
            strengths[voice].append(strength)
            assigned[voice].append(max(association, default=0))
            above[voice] += prob > theta
    if total == 0:
        raise ValueError("no actual two-voice overlap in oracle")
    return {
        "oracle_two_voice_hops": total,
        "oracle_voice_separation_deg": distribution(distances),
        "angular_tolerance_deg": tolerance,
        "two_distinct_ssl_direction_hops": sum(matched),
        "distinct_ssl_streaks": streaks(matched),
        "two_active_sst_tracks_hops": two_active,
        "any_two_candidates_above_birth_threshold_hops": any_two_above,
        "matched_pair_both_above_birth_threshold_hops": matched_two_above,
        "speaker_direction_matches": {
            "speaker_1": {"birth_posterior": distribution(probs[0]),
                          "strength": distribution(strengths[0]),
                          "max_existing_track_posterior": distribution(assigned[0]),
                          "above_birth_threshold_hops": above[0]},
            "speaker_2": {"birth_posterior": distribution(probs[1]),
                          "strength": distribution(strengths[1]),
                          "max_existing_track_posterior": distribution(assigned[1]),
                          "above_birth_threshold_hops": above[1]}},
        "separation_strata": by_sep,
        "limitation": "geometric ODAS candidate evidence only; no speaker identity, "
                      "clean source separation, IDF1, SI-SDRi, SIRi or ASR WER",
    }


def score_artifact(root, tolerance=25.0):
    case = Path(root) / "t03_eval_two"
    wav = case / "render/input.wav"
    sha = hashlib.sha256(wav.read_bytes()).hexdigest()
    if sha != (case / "input_sha256.txt").read_text().strip():
        raise ValueError("changed input microphone WAV SHA256")
    gt = json.loads((case / "render/ground_truth.json").read_text())
    summary = json.loads((case / "odas/summary.json").read_text())
    with (case / "odas/sst_raw_trace.jsonl").open() as src:
        rows = [json.loads(line) for line in src]
    if (len(rows) != summary["hops"] or
            summary["input_frames"] != gt["wav_frames"] or
            summary["recorder_drops"] != 0 or
            len(rows) * 128 != summary["input_frames"]):
        raise ValueError("incomplete ODAS PCM/hop integrity")
    return {"schema": "libaudition.acoustix-s2-ssl-birth-evidence.v1",
            "input_pcm_sha256": sha,
            "validated_real_odas_hops": len(rows),
            "diagnostics": analyse(rows, gt["ground_truth"], tolerance)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tolerance", type=float, default=25.0)
    args = parser.parse_args()
    result = score_artifact(args.artifact_root, args.tolerance)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
