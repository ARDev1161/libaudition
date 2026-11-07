#!/usr/bin/env python3
"""Diagnostic, one-to-one source/ODAS association for Acoustix test fixtures.

Only standard-library dependencies. These statistics are *not* calibrated
accuracy gates: the synthetic voices are not measured speech, the RIRs are
approximations, and the array is uncalibrated.
"""
from __future__ import annotations

from collections import defaultdict
from functools import lru_cache
import math
import statistics


def angular_error_degrees(oracle: dict, track: dict) -> float:
    azimuth = math.radians(float(oracle["bearing_deg"]))
    elevation = math.radians(float(oracle.get("elevation_deg", 0.0)))
    expected = (math.cos(elevation) * math.cos(azimuth),
                math.cos(elevation) * math.sin(azimuth),
                math.sin(elevation))
    vector = tuple(float(track[axis]) for axis in ("x", "y", "z"))
    norm = math.sqrt(sum(v * v for v in vector))
    if norm < 1e-9 or not math.isfinite(norm):
        return 180.0
    dot = sum(x * y for x, y in zip(expected, vector)) / norm
    return math.degrees(math.acos(max(-1.0, min(1.0, dot))))


def match_sources_to_tracks(sources: list, tracks: list,
                            max_error_deg: float = 45.0) -> list:
    """Max-cardinality, minimum-total-error unique assignment.

    A track cannot satisfy two speakers; a speaker cannot claim two tracks.
    The small set of seven speakers is solved exactly by bitmask DP. The
    matching tolerance is solely a diagnostic convention, not a calibrated
    microphone accuracy acceptance threshold.
    """
    if not sources or not tracks:
        return []
    distances = [[angular_error_degrees(src, track) for src in sources]
                 for track in tracks]

    @lru_cache(maxsize=None)
    def solve(track_index: int, used: int):
        if track_index == len(tracks):
            return 0, 0.0, ()
        best = solve(track_index + 1, used)
        for i in range(len(sources)):
            if used & (1 << i) or distances[track_index][i] > max_error_deg:
                continue
            tail = solve(track_index + 1, used | (1 << i))
            proposal = (tail[0] + 1, tail[1] + distances[track_index][i],
                        ((i, track_index, distances[track_index][i]),) + tail[2])
            if (proposal[0] > best[0] or
                    (proposal[0] == best[0] and proposal[1] < best[1])):
                best = proposal
        return best

    return [(sources[s], tracks[t], error) for s, t, error in solve(0, 0)[2]]


def compute_metrics(ground_truth: list, observations: list, summary: dict,
                    max_error_deg: float = 45.0,
                    silence_start_frame: int | None = None) -> dict:
    by_hop = defaultdict(list)
    for obs in observations:
        if obs.get("kind") == "track":
            by_hop[int(obs["hop"])].append(obs)
    active_hops = matched_hops = 0
    total_voice_hops = matched_voice_hops = 0
    per_voice = defaultdict(lambda: {"active_hops": 0, "matched_hops": 0})
    adjacent_id_switches = 0
    adjacent_comparisons = 0
    last_match = {}
    errors = []
    azimuth_errors = []
    simultaneous = []
    all_counts = []
    max_active_voices = 0
    for point in ground_truth:
        hop = int(point["hop"])
        voices = [src for src in point["sources"] if src["kind"] == "voice"]
        tracks = by_hop.get(hop, [])
        all_counts.append(len(tracks))
        max_active_voices = max(max_active_voices, len(voices))
        if not voices:
            continue
        active_hops += 1
        total_voice_hops += len(voices)
        if len(voices) > 1:
            simultaneous.append(len(tracks))
        matches = match_sources_to_tracks(voices, tracks, max_error_deg)
        matched_voice_hops += len(matches)
        if matches:
            matched_hops += 1
        ids = {str(source["id"]) for source, _, _ in matches}
        for source in voices:
            per_voice[str(source["id"])]["active_hops"] += 1
            if str(source["id"]) in ids:
                per_voice[str(source["id"])]["matched_hops"] += 1
        for source, track, error in matches:
            sid = str(source["id"])
            track_id = str(track["id"])
            errors.append(error)
            # A flat robot-mounted array has weak elevation observability.
            # Report horizontal bearing error separately for navigation;
            # do not count a nearly vertical, undefined-azimuth direction.
            tx, ty = float(track["x"]), float(track["y"])
            if math.hypot(tx, ty) > 1e-6:
                track_az = math.degrees(math.atan2(ty, tx))
                azimuth_errors.append(abs(
                    (track_az - float(source["bearing_deg"]) + 180) % 360 - 180))
            old = last_match.get(sid)
            if old is not None and old[0] == hop - 1:
                adjacent_comparisons += 1
                adjacent_id_switches += int(old[1] != track_id)
            last_match[sid] = (hop, track_id)

    errors.sort()
    azimuth_errors.sort()
    report = {
        "scope": "diagnostic-only; synthetic image-source room; no calibration",
        "association_threshold_deg": max_error_deg,
        "ground_truth_hops": len(ground_truth),
        "active_voice_hops": active_hops,
        "active_voice_source_hops": total_voice_hops,
        "matched_voice_source_hops": matched_voice_hops,
        "matched_voice_source_fraction": round(matched_voice_hops / total_voice_hops, 6)
            if total_voice_hops else None,
        "voice_hops_with_at_least_one_match": matched_hops,
        "voice_hops_with_match_fraction": round(matched_hops / active_hops, 6)
            if active_hops else None,
        "max_active_voices": max_active_voices,
        "max_simultaneous_tracks": max(all_counts, default=0),
        "mean_tracks_during_multi_voice": round(statistics.mean(simultaneous), 4)
            if simultaneous else None,
        "matched_median_3d_angular_error_deg": round(statistics.median(errors), 4)
            if errors else None,
        "matched_p90_3d_angular_error_deg": round(
            errors[min(len(errors) - 1, math.ceil(.9 * len(errors)) - 1)], 4)
            if errors else None,
        "matched_median_azimuth_error_deg":
            round(statistics.median(azimuth_errors), 4) if azimuth_errors else None,
        "matched_p90_azimuth_error_deg": round(
            azimuth_errors[min(len(azimuth_errors) - 1,
                               math.ceil(.9 * len(azimuth_errors)) - 1)], 4)
            if azimuth_errors else None,
        "adjacent_matched_id_switches": adjacent_id_switches,
        "adjacent_matched_id_comparisons": adjacent_comparisons,
        "per_voice": dict(sorted(per_voice.items())),
        "track_observations": int(summary["track_observations"]),
        "potential_observations": int(summary["potential_observations"]),
        "separated_frames": int(summary["separated_frames"]),
        "recordings": int(summary["recordings"]),
        "recorder_drops": int(summary["recorder_drops"]),
    }
    if silence_start_frame is not None:
        # Only digital silence on the actual four microphone channels counts.
        # ODAS tracking may legitimately linger after the input disappears;
        # record the hold time rather than arbitrarily failing the test.
        if silence_start_frame < 0:
            raise ValueError("silence_start_frame must be nonnegative")
        first_silent_hop = math.ceil(silence_start_frame / 128)
        late = [obs for obs in observations
                if obs.get("kind") == "track" and int(obs["hop"]) >= first_silent_hop]
        rate = int(summary["sample_rate_hz"])
        report["microphone_digital_silence_start_s"] = round(
            silence_start_frame / rate, 4)
        report["microphone_digital_silence_track_observations"] = len(late)
        report["microphone_digital_silence_distinct_track_ids"] = len(
            {str(obs["id"]) for obs in late})
        report["microphone_digital_silence_tracked_hops"] = len(
            {int(obs["hop"]) for obs in late})
        report["microphone_digital_silence_track_last_seen_s"] = (
            round(max(int(obs["hop"]) for obs in late) * 128 / rate, 4)
            if late else None)
        report["microphone_digital_silence_available_hops"] = max(
            0, int(summary["input_frames"]) // 128 - first_silent_hop)
    return report
