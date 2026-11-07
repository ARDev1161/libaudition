#!/usr/bin/env python3
"""Audit measured ODAS SST probation against the *frozen* Acoustix voice oracle.

Not source separation, speaker diarization, SIR/SI-SDR or ASR quality.
Parameters are explicitly supplied from the pinned offline runner's OdasOptions;
the v1 ODAS trace does not record them. No thresholds are auto-tuned here.
"""
import argparse
import json
import math
import statistics
from pathlib import Path

from acoustix_s2_sst_trace_compare import recorded_rows, sha256

CASES = ("t02_dev_single", "t03_eval_two", "t07_eval_seven")


class AuditError(ValueError):
    pass


def _unit_direction(bearing_deg, elevation_deg):
    b, e = math.radians(bearing_deg), math.radians(elevation_deg)
    return (math.cos(e) * math.cos(b),
            math.cos(e) * math.sin(b), math.sin(e))


def _angle_deg(a, b):
    dot = sum(x * y for x, y in zip(a, b))
    return math.degrees(math.acos(max(-1.0, min(1.0, dot))))


def _two_distinct_ssl_candidates(sources, candidates, limit_deg):
    """One-to-one geometric candidate coverage; NOT separated speech quality."""
    if len(sources) != 2:
        raise AuditError("exactly two oracle voice sources required")
    truth = [_unit_direction(s["bearing_deg"], s["elevation_deg"]) for s in sources]
    proposals = [(c["x"], c["y"], c["z"]) for c in candidates]
    return any(_angle_deg(truth[0], a) <= limit_deg
               and _angle_deg(truth[1], b) <= limit_deg
               for i, a in enumerate(proposals)
               for j, b in enumerate(proposals) if i != j)


def _candidate_track_union(record, track_id):
    """Match ODAS arrayTrackTotal = 1 - product(1 - P(track|SSL-pot))."""
    values = []
    for candidate in record["ssl_candidates"]:
        hits = [p["posterior"] for p in candidate["track_posteriors"]
                if p["track_id"] == track_id]
        if len(hits) != 1:
            raise AuditError(f"hop {record['hop_index']}: prior track {track_id} "
                             "missing or duplicated in SSL marginals")
        value = hits[0]
        if (type(value) not in (float, int) or not math.isfinite(value)
                or not 0 <= value <= 1):
            raise AuditError("invalid prior-track posterior")
        values.append(value)
    if not values:
        raise AuditError("empty SSL candidate list: cannot infer probation posterior")
    return 1.0 - math.prod(1.0 - value for value in values)


def probation_decisions(rows, window, threshold, oracle_hops=None):
    """Reconstruct exact five-hop probation from recorded per-pot posteriors."""
    if type(window) is not int or window < 1 or not 0 <= threshold <= 1:
        raise AuditError("invalid probation configuration")
    births = {}
    retires = {}
    for index, record in enumerate(rows):
        if record["hop_index"] != index or record["sample_offset"] != 128 * index:
            raise AuditError("noncontiguous recorded 128-sample SST timeline")
        for track_id in record["births"]:
            if track_id in births:
                raise AuditError(f"duplicate birth ID {track_id}")
            births[track_id] = index
        for track_id in record["retirements"]:
            if track_id in retires:
                raise AuditError(f"duplicate retirement ID {track_id}")
            retires[track_id] = index
    events = []
    for track_id, birth_hop in births.items():
        if birth_hop + window >= len(rows):
            events.append({"id": track_id, "birth_hop": birth_hop,
                           "decision": "INCOMPLETE", "probation_mean": None})
            continue
        posterior = [_candidate_track_union(rows[hop], track_id)
                     for hop in range(birth_hop + 1, birth_hop + window + 1)]
        mean = statistics.mean(posterior)
        decision_hop = birth_hop + window
        current = rows[decision_hop]
        active = any(t["track_id"] == track_id and t["state"] == "A"
                     for t in current["tracks_after"])
        retired = track_id in current["retirements"]
        if active == retired:
            raise AuditError(f"ID {track_id}: neither or both active and retired "
                             "at completed probation window")
        # A float-rounding boundary cannot be diagnosed from decimal JSONL.
        if abs(mean - threshold) < 0.00005:
            predicted = "AMBIGUOUS_ROUNDING"
        else:
            predicted = "ACTIVE" if mean > threshold else "RETIRED"
            if (predicted == "ACTIVE") != active:
                raise AuditError(f"ID {track_id}: observed probation decision "
                                 f"contradicts independent marginal calculation")
        actual = "ACTIVE" if active else "RETIRED"
        if retired and retires.get(track_id) != decision_hop:
            raise AuditError(f"ID {track_id}: missing retirement at decision hop")
        overlap = None
        if oracle_hops is not None and birth_hop < len(oracle_hops):
            overlap = sum(s.get("kind") == "voice"
                          for s in oracle_hops[birth_hop]["sources"]) >= 2
        events.append({
            "id": track_id, "birth_hop": birth_hop,
            "decision_hop": decision_hop, "decision": actual,
            "predicted": predicted, "probation_mean": round(mean, 8),
            "probation_hop_posteriors": [round(v, 8) for v in posterior],
            "birth_during_oracle_multivoice": overlap,
        })
    return events


def audit_case(case_dir, window, threshold):
    raw = case_dir / "odas/sst_raw_trace.jsonl"
    rows = recorded_rows(raw)
    oracle = json.loads((case_dir / "render/ground_truth.json").read_text())
    schedule = oracle["speech_schedule"]
    gt = oracle["ground_truth"]
    summary = json.loads((case_dir / "odas/summary.json").read_text())
    wav = case_dir / "render/input.wav"
    checksum = sha256(wav)
    if checksum != (case_dir / "input_sha256.txt").read_text().strip():
        raise AuditError("frozen Acoustix WAV hash mismatch")
    if (len(rows) != int(summary["hops"])
            or summary["recorder_drops"] != 0
            or summary["input_frames"] != oracle["wav_frames"]
            or len(rows) * 128 != int(summary["input_frames"])):
        raise AuditError("ODAS input PCM/hop/drop integrity failure")
    if not gt or len(gt) > len(rows):
        raise AuditError("empty or oversized Acoustix oracle")
    if any(hop["hop"] != i for i, hop in enumerate(gt)):
        raise AuditError("noncontiguous Acoustix oracle timeline")
    if schedule["voice_source_count"] not in (1, 2, 7):
        raise AuditError("unexpected real-speech oracle configuration")

    events = probation_decisions(rows, window, threshold, gt)
    promoted = [e for e in events if e["decision"] == "ACTIVE"]
    rejected = [e for e in events if e["decision"] == "RETIRED"]
    incomplete = [e for e in events if e["decision"] == "INCOMPLETE"]
    two_voice_hops, two_active_hops = 0, 0
    pair_separations = []
    coverage_25, coverage_45 = 0, 0
    for i, oracle_hop in enumerate(gt):
        sources = [s for s in oracle_hop["sources"] if s.get("kind") == "voice"]
        if len(sources) != 2:
            continue
        two_voice_hops += 1
        two_active_hops += sum(t["state"] == "A"
                               for t in rows[i]["tracks_after"]) >= 2
        v1, v2 = (_unit_direction(s["bearing_deg"], s["elevation_deg"])
                  for s in sources)
        pair_separations.append(_angle_deg(v1, v2))
        coverage_25 += _two_distinct_ssl_candidates(
            sources, rows[i]["ssl_candidates"], 25)
        coverage_45 += _two_distinct_ssl_candidates(
            sources, rows[i]["ssl_candidates"], 45)
    return {
        "input_sha256": checksum,
        "real_recorded_hops": len(rows),
        "oracle_hops": len(gt),
        "oracle_two_voice_hops": two_voice_hops,
        "oracle_two_voice_median_angular_separation_deg":
            round(statistics.median(pair_separations), 4) if pair_separations else None,
        "two_distinct_ssl_candidates_within_25_deg_hops": coverage_25,
        "two_distinct_ssl_candidates_within_45_deg_hops": coverage_45,
        "two_active_sst_tracks_during_two_voice_hops": two_active_hops,
        "total_provisional_births": len(events),
        "confirmed_births": len(promoted),
        "rejected_births": len(rejected),
        "incomplete_births": len(incomplete),
        "provisional_births_during_oracle_multivoice":
            sum(e["birth_during_oracle_multivoice"] is True for e in events),
        "max_active_tracks": max(
            sum(t["state"] == "A" for t in row["tracks_after"])
            for row in rows),
        "probation_events": events,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--probation-window", required=True, type=int)
    parser.add_argument("--probation-threshold", required=True, type=float)
    args = parser.parse_args()
    if not args.root.is_dir():
        raise AuditError("missing real Acoustix S2 artifact root")
    if args.probation_window < 1 or not 0 <= args.probation_threshold <= 1:
        raise AuditError("invalid explicit ODAS option values")
    result = {
        "schema": "libaudition.acoustix-s2-odas-probation-audit.v1",
        "parameter_provenance": "explicit pinned offline runner OdasOptions values",
        "probation_window": args.probation_window,
        "probation_threshold": args.probation_threshold,
        "limits": "Geometric candidate diagnostics and tracker probation only; "
                  "NOT isolated speech, speaker ID, SI-SDRi/SIRi or ASR WER",
        "cases": {case: audit_case(args.root / case,
                                  args.probation_window,
                                  args.probation_threshold)
                  for case in CASES},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    for case, report in result["cases"].items():
        print(json.dumps({
            "case": case,
            "provisional_births": report["total_provisional_births"],
            "confirmed": report["confirmed_births"],
            "rejected": report["rejected_births"],
            "oracle_two_voice_hops": report["oracle_two_voice_hops"],
            "two_ssl_proposals_25_deg_hops":
                report["two_distinct_ssl_candidates_within_25_deg_hops"],
            "two_active_tracks_in_two_voice_hops":
                report["two_active_sst_tracks_during_two_voice_hops"],
        }, sort_keys=True))


if __name__ == "__main__":
    main()
