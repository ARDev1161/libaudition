#!/usr/bin/env python3
"""Compare *recorded* ODAS SST decisions on bit-identical Acoustix S2 WAVs.

This is NOT a speech-separation score, IDF1, SI-SDRi or ASR benchmark.
Fail closed on mismatched WAV hashes, missing traces, or broken hop clocks.
"""
import argparse
import hashlib
import json
from pathlib import Path


CASES = ("t02_dev_single", "t03_eval_two", "t07_eval_seven")


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def recorded_rows(path):
    if not path.is_file():
        raise ValueError(f"missing recorded trace: {path}")
    rows = []
    with path.open(encoding="utf-8") as handle:
        for idx, line in enumerate(handle):
            record = json.loads(line)
            if (record["schema"] != "odas_sst_raw_v1"
                    or record["hop_index"] != idx
                    or record["odas_timestamp"] != idx + 1
                    or record["sample_offset"] != 128 * idx
                    or record["hop_frames"] != 128):
                raise ValueError(f"noncontiguous raw observation at {path}:{idx + 1}")
            rows.append(record)
    if not rows:
        raise ValueError(f"no measured ODAS hops in {path}")
    return rows


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def trace_stats(rows, speech_schedule):
    events = []
    active_tracks_by_hop = []
    pending_tracks_by_hop = []
    multiple_birth_posteriors = 0
    birth_capacity_limited_hops = 0
    provisional_ids = set()
    promoted_ids = set()
    schedule = speech_schedule.get("segments", [])
    overlap_hops = 0
    overlap_hops_two_active = 0

    for row in rows:
        candidates = row["ssl_candidates"]
        eligible = sum(c["birth_posterior"] > row["theta_new"]
                       for c in candidates)
        previous_count = len(candidates[0]["track_posteriors"]) if candidates else 0
        if eligible >= 2:
            multiple_birth_posteriors += 1
        if eligible > len(row["births"]) and previous_count + eligible > 4:
            birth_capacity_limited_hops += 1
        events.extend(row["births"])
        tracks = row["tracks_after"]
        active_count = sum(t["state"] == "A" for t in tracks)
        pending_count = sum(t["state"] == "P" for t in tracks)
        active_tracks_by_hop.append(active_count)
        pending_tracks_by_hop.append(pending_count)
        provisional_ids.update(t["track_id"] for t in tracks if t["state"] == "P")
        promoted_ids.update(t["track_id"] for t in tracks if t["state"] == "A")
        frame = row["sample_offset"]
        if len(schedule) >= 2 and all(
                seg["scheduled_start_frame"] <= frame < seg["placed_end_frame"]
                for seg in schedule):
            overlap_hops += 1
            overlap_hops_two_active += active_count >= 2

    return {
        "validated_raw_hops": len(rows),
        "birth_events": len(events),
        "unique_birth_ids": len(set(events)),
        "max_active_tracks": max(active_tracks_by_hop),
        "max_provisional_tracks": max(pending_tracks_by_hop),
        "provisional_ids": len(provisional_ids),
        "provisional_ids_ever_active": len(provisional_ids & promoted_ids),
        "hops_with_at_least_two_eligible_birth_candidates": multiple_birth_posteriors,
        "birth_pressure_capacity_limited_hops": birth_capacity_limited_hops,
        "scheduled_two_speaker_overlap_hops": overlap_hops,
        "scheduled_overlap_hops_with_two_active_tracks": overlap_hops_two_active,
    }


def compare(baseline, candidate):
    results = {}
    for name in CASES:
        old_case = baseline / name
        new_case = candidate / name
        old_wav, new_wav = old_case / "render/input.wav", new_case / "render/input.wav"
        old_hash, new_hash = sha256(old_wav), sha256(new_wav)
        declared_old = (old_case / "input_sha256.txt").read_text().strip()
        declared_new = (new_case / "input_sha256.txt").read_text().strip()
        if old_hash != new_hash or old_hash != declared_old or old_hash != declared_new:
            raise ValueError(f"{name}: A/B replay WAV not byte-identical")
        old_schedule = load_json(old_case / "render/speech_schedule.json")
        new_schedule = load_json(new_case / "render/speech_schedule.json")
        if old_schedule != new_schedule:
            raise ValueError(f"{name}: A/B speech schedule changed")
        old_rows = recorded_rows(old_case / "odas/sst_raw_trace.jsonl")
        new_rows = recorded_rows(new_case / "odas/sst_raw_trace.jsonl")
        if len(old_rows) != len(new_rows):
            raise ValueError(f"{name}: A/B ODAS hop counts differ")
        old_summary = load_json(old_case / "odas/summary.json")
        new_summary = load_json(new_case / "odas/summary.json")
        if (old_summary["input_frames"] != new_summary["input_frames"]
                or old_summary["recorder_drops"] != 0
                or new_summary["recorder_drops"] != 0):
            raise ValueError(f"{name}: input-frame/recorder-drop integrity changed")
        results[name] = {
            "identical_pcm_sha256": old_hash,
            "input_frames": old_summary["input_frames"],
            "before": trace_stats(old_rows, old_schedule),
            "after": trace_stats(new_rows, new_schedule),
            "before_max_recorded_active_tracks":
                load_json(old_case / "odas/source_track_metrics.json")["max_simultaneous_tracks"],
            "after_max_recorded_active_tracks":
                load_json(new_case / "odas/source_track_metrics.json")["max_simultaneous_tracks"],
        }
        if name == "t03_eval_two":
            before = old_rows[699]
            after = new_rows[699]
            results[name]["hop699"] = {
                "before_birth_ids": before["births"],
                "after_birth_ids": after["births"],
                "before_eligible_candidates": sum(
                    c["birth_posterior"] > before["theta_new"]
                    for c in before["ssl_candidates"]),
                "after_eligible_candidates": sum(
                    c["birth_posterior"] > after["theta_new"]
                    for c in after["ssl_candidates"]),
            }
    return {
        "schema": "libaudition.real_speech_sst_slot_fix_paired.v1",
        "comparison": "actual identical Acoustix WAV replay, before and after ODAS slot fix",
        "interpretation": "diagnostic only; no source-separation or ASR quality PASS",
        "cases": results,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline_dir", type=Path)
    parser.add_argument("candidate_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = compare(args.baseline_dir, args.candidate_dir)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
