#!/usr/bin/env python3
"""Real frozen Acoustix ODAS prior A/B: normal vs near-horizontal directions.

Does NOT assert speech separation: two SST tracks must each match a
different true speaker in 3-D, and even that is only geometry.
This experiment reuses the exact same native C++ runner and original
licensed LibriSpeech WAVs; no RIR or PCM generation apart from the
separately labelled synthetic T00 digital-zero control.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

from acoustix_s2_gmm_controlled_sweep import (
    REAL_CASES, EXPECTED, SweepError, file_sha256,
    oracle_overlap_frames, run_one,
)
from acoustix_s2_gmm_doa_audit import (
    measure_t03_oracle_pairs, read_native_tracks,
)


def active_verticality(csv_path):
    by_hop = read_native_tracks(csv_path)
    elevations = []
    for tracks in by_hop.values():
        for t in tracks:
            z = float(t["z"])
            if not math.isfinite(z):
                raise SweepError("nonfinite active ODAS vertical direction")
            norm = math.sqrt(sum(float(t[k]) ** 2 for k in ("x", "y", "z")))
            if not 0.98 <= norm <= 1.02:
                raise SweepError("nonunit active ODAS vector")
            elevations.append(math.degrees(math.asin(max(-1.0, min(1.0, z / norm)))))
    return {
        "active_track_observations": len(elevations),
        "active_track_observations_above_45deg_absolute_elevation":
            sum(abs(e) > 45.0 for e in elevations),
        "median_absolute_elevation_deg":
            round(statistics.median(abs(e) for e in elevations), 6)
            if elevations else None,
    }


def assert_source_integrity(root, case):
    wav = root / case / "render/input.wav"
    sha = file_sha256(wav)
    if sha != (root / case / "input_sha256.txt").read_text().strip():
        raise SweepError(f"{case}: source real WAV changed")
    return wav, sha


def analyze_variant(report, config, mode, case, metrics, csv_path,
                    wav_sha, trace_sha, oracle):
    geom = None
    if case == "t03_eval_two":
        geom = measure_t03_oracle_pairs(oracle, read_native_tracks(csv_path))
        if geom["oracle_exactly_two_voice_hops"] != metrics["oracle_exactly_two_voice_hops"]:
            raise SweepError("T03 geometric and SST oracle overlap clocks disagree")
        if (geom["two_or_more_active_tracks_in_overlap_hops"] !=
                metrics["oracle_two_voice_hops_with_at_least_two_active_tracks"]):
            raise SweepError("T03 active state and native CSV disagree")
    report[case] = {
        "input_wav_sha256": wav_sha,
        "native_trace_sha256": trace_sha,
        "parameters": config,
        "horizontal_source_prior_enabled": mode,
        "tracker_metrics": metrics,
        "native_3d_track_elevation": active_verticality(csv_path),
        "t03_distinct_speaker_3d_matches": geom,
    }


def evaluate(root, native_runner, validator, output):
    root, native_runner, validator, output = map(
        Path, (root, native_runner, validator, output))
    if output.exists():
        raise SweepError("Refusing overwrite of previous horizontal prior evidence")
    if not native_runner.is_file() or not validator.is_file():
        raise SweepError("Missing genuine ODAS native runner or pinned trace validator")
    prev = json.loads((root / "gmm-sweep/gmm_sweep_report.json").read_text())
    if prev["schema"] != "libaudition.s2_native_gmm_sensitivity.v1":
        raise SweepError("Missing previously verified real native GMM matrix")
    case_wavs = {case: assert_source_integrity(root, case) for case in REAL_CASES}
    t00 = root / "gmm-sweep/t00_synthetic_zero_pcm.wav"
    if file_sha256(t00) != prev["variants"]["baseline"]["cases"]["t00_zero_pcm_control"]["wav_sha256"]:
        raise SweepError("T00 synthetic zero PCM was altered")
    with __import__("wave").open(str(t00), "rb") as wav:
        if (wav.getnchannels() != 6 or wav.getsampwidth() != 2 or
                wav.getframerate() != 16000 or wav.getnframes() != 240000 or
                any(wav.readframes(240000))):
            raise SweepError("T00 synthetic silence contains nonzero audio")
    case_wavs["t00_zero_pcm_control"] = (t00, file_sha256(t00))
    overlap = oracle_overlap_frames(root / "t03_eval_two")
    oracle = json.loads((root / "t03_eval_two/render/ground_truth.json").read_text())["ground_truth"]
    output.mkdir(parents=True)
    result = {
        "schema": "libaudition.s2_horizontal_prior_ab_v1",
        "baseline": "same-job native PCM and GMM sweep; all WAV bytes identical",
        "negative_control": "SYNTHETIC zero PCM, not measured room noise",
        "scope": "two distinct 3-D active-track matches vs 2 oracle human directions; NOT separated voice audio or WER",
        "variants": {},
    }
    variants = (
        ("baseline_off", "baseline", False, ()),
        ("baseline_plane", "baseline", True, ("--horizontal-sources",)),
        ("active_var_001_off", "active_var_001", False, ("--sst-active-variance", "0.01")),
        ("active_var_001_plane", "active_var_001", True,
         ("--horizontal-sources", "--sst-active-variance", "0.01")),
    )
    for name, gmm, horizontal, args in variants:
        cases = {}
        config = EXPECTED[gmm]
        for case, (wav, sha) in case_wavs.items():
            if not horizontal:
                # Baseline/control from exact same CI run, not rerendered.
                native_root = (root / case / "odas" if gmm == "baseline" and
                               case in REAL_CASES else
                               root / "gmm-sweep" / gmm / case)
                old = prev["variants"][gmm]["cases"][case]
                if old["wav_sha256"] != sha:
                    raise SweepError("Same-job existing GMM source WAV SHA differs")
                metrics = old["metrics"]
                trace_sha = old["trace_sha256"]
            else:
                native_root = output / name / case
                metrics = run_one(
                    native_runner, validator, wav, native_root, args, sha,
                    overlap if case == "t03_eval_two" else None, config)
                trace_sha = file_sha256(native_root / "sst_raw_trace.jsonl")
            summary = json.loads((native_root / "summary.json").read_text())
            if summary.get("horizontal_sources") is not horizontal:
                raise SweepError(f"{name}/{case} did not actually use expected direction prior")
            if summary["recorder_drops"] or summary["input_frames"] != 240000:
                raise SweepError("Native ODAS frame/drop integrity failure")
            analyze_variant(cases, config, horizontal, case, metrics,
                            native_root / "observations.csv", sha, trace_sha, oracle)
        result["variants"][name] = {"gmm": gmm, "horizontal_sources": horizontal,
                                    "cli": list(args), "cases": cases}
        c = cases["t03_eval_two"]["t03_distinct_speaker_3d_matches"]
        print(json.dumps({
            "variant": name,
            "t03_two_active_hops": c["two_or_more_active_tracks_in_overlap_hops"],
            "t03_two_true_voices_within_45deg": c["two_distinct_track_to_voice_matches_within_45deg_hops"],
            "t03_vertical_track_observations_over_45deg":
                cases["t03_eval_two"]["native_3d_track_elevation"]["active_track_observations_above_45deg_absolute_elevation"],
            "t02_max_active":
                cases["t02_dev_single"]["tracker_metrics"]["max_simultaneous_active_tracks"],
            "t00_synthetic_births":
                cases["t00_zero_pcm_control"]["tracker_metrics"]["birth_events"],
        }, sort_keys=True), flush=True)
    (output / "horizontal_prior_report.json").write_text(
        json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--artifact-root", type=Path, required=True)
    p.add_argument("--runner", type=Path, required=True)
    p.add_argument("--validator", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    try:
        evaluate(args.artifact_root, args.runner, args.validator, args.output)
    except (OSError, ValueError, KeyError) as exc:
        p.exit(2, f"BLOCKED: {exc}\n")


if __name__ == "__main__":
    main()
