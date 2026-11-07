#!/usr/bin/env python3
"""Real human-speech ODAS capacity sweep on immutable Acoustix microphone WAV.

Runs 4x4 / 4x8 / 8x4 / 8x8 / 8x8_sst4 on SAME 16kHz microphone file.
Comparisons are diagnostics: increasing capacity does not by itself prove
spatial separation, real speaker ID, SI-SDR or ASR word recognition.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from acoustix_metrics import compute_metrics

SCHEMA = "libaudition.acoustix-s2-odas-capacity.v1"
CONFIGS = (
    ("4x4", 4, 4, 0),
    ("4x8", 4, 8, 0),
    ("8x4", 8, 4, 0),
    ("8x8", 8, 8, 0),
    ("8x8_sst4", 8, 8, 4),
)
SCENES = ("t03_eval_two", "t07_eval_seven")


def proposal_signature(observations: list[dict]) -> list[tuple]:
    """Raw SSL may not change when ONLY downstream SST input is capped."""
    keys = ("hop", "id", "x", "y", "z", "activity")
    return [tuple(row.get(key, "") for key in keys)
            for row in observations if row.get("kind") == "proposal"]


def summarize_capacity(rows: list[dict], summary: dict,
                       oracle: dict) -> dict:
    metric = compute_metrics(
        oracle["ground_truth"], rows, summary)
    return {
        "configured_ssl_candidates": summary["ssl_potential_source_count"],
        "configured_sst_max_tracks": summary["sst_max_tracks"],
        "configured_sst_input_limit": summary["sst_input_potential_limit"],
        "ssl_proposals": metric["potential_observations"],
        "sst_track_observations": metric["track_observations"],
        "max_simultaneous_tracks": metric["max_simultaneous_tracks"],
        "oracle_active_voice_source_hops": metric["active_voice_source_hops"],
        "matched_voice_source_hops": metric["matched_voice_source_hops"],
        "matched_voice_source_fraction": metric["matched_voice_source_fraction"],
        "matched_azimuth_median_deg": metric["matched_median_azimuth_error_deg"],
        "matched_azimuth_p90_deg": metric["matched_p90_azimuth_error_deg"],
        "adjacent_matched_id_switches": metric["adjacent_matched_id_switches"],
        "recordings": metric["recordings"],
        "recorder_drops": metric["recorder_drops"],
        "input_frames": summary["input_frames"],
        "full_details": metric,
    }


def run_sweep(runner: Path, root: Path, output: Path) -> bool:
    baseline = json.loads((root / "real_speech_odas_diagnostics.json").read_text())
    if not baseline.get("pipeline_integrity_passed"):
        raise ValueError("Original real-speech ODAS baseline is not integrity-clean")
    if not runner.is_file():
        raise ValueError("Compiled ODAS offline runner does not exist")
    output.mkdir(parents=True, exist_ok=True)
    results = []
    errors = []
    for scene_name in SCENES:
        scene_folder = root / scene_name
        input_file = scene_folder / "render" / "input.wav"
        fixed_hash = (scene_folder / "input_sha256.txt").read_text().strip()
        if hashlib.sha256(input_file.read_bytes()).hexdigest() != fixed_hash:
            raise ValueError("Changed real speech input: " + scene_name)
        oracle = json.loads((scene_folder / "render" / "ground_truth.json").read_text())
        per_configuration = {}
        raw_ssl = {}
        scene_output = output / scene_name
        scene_output.mkdir(parents=True, exist_ok=True)
        for label, ssl_candidates, sst_tracks, sst_cap in CONFIGS:
            folder = scene_output / label
            folder.mkdir(parents=True, exist_ok=True)
            command = [
                str(runner.resolve()), str(input_file.resolve()), str(folder.resolve()),
                "--max-tracks", str(sst_tracks),
                "--potential-sources", str(ssl_candidates),
                "--sst-input-potentials", str(sst_cap),
            ]
            try:
                job = subprocess.run(command, capture_output=True, text=True,
                                     timeout=240, check=False)
                (folder / "runner.stdout.txt").write_text(job.stdout)
                (folder / "runner.stderr.txt").write_text(job.stderr)
                if job.returncode:
                    raise RuntimeError(
                        f"offline runner exit {job.returncode}: " + job.stderr[-900:])
                summary = json.loads((folder / "summary.json").read_text())
                if (int(summary["input_frames"]) != int(oracle["wav_frames"])
                        or int(summary["recorder_drops"]) != 0):
                    raise RuntimeError("ODAS replay frame integrity or recorder drops")
                if (int(summary["ssl_potential_source_count"]) != ssl_candidates
                        or int(summary["sst_max_tracks"]) != sst_tracks
                        or int(summary["sst_input_potential_limit"]) != sst_cap):
                    raise RuntimeError("ODAS actual capacity does not match requested config")
                with (folder / "observations.csv").open(newline="") as source:
                    observations = list(csv.DictReader(source))
                record = summarize_capacity(observations, summary, oracle)
                record["passed_integrity"] = True
                per_configuration[label] = record
                raw_ssl[label] = proposal_signature(observations)
                (folder / "metrics.json").write_text(
                    json.dumps(record["full_details"], indent=2) + "\n")
                print(json.dumps({
                    "scene": scene_name, "capacity": label,
                    "max_simultaneous_tracks": record["max_simultaneous_tracks"],
                    "matched_fraction": record["matched_voice_source_fraction"],
                    "ssl_proposals": record["ssl_proposals"],
                    "recorder_drops": record["recorder_drops"],
                    "passed_integrity": True
                }), flush=True)
            except (RuntimeError, OSError, ValueError, KeyError,
                    subprocess.TimeoutExpired) as exc:
                per_configuration[label] = {
                    "passed_integrity": False,
                    "error": f"{type(exc).__name__}: {exc}",
                }
                errors.append(f"{scene_name}/{label}: {exc}")
                print(json.dumps({"scene": scene_name, "capacity": label,
                                  "passed_integrity": False, "error": str(exc)}),
                      flush=True)
            if hashlib.sha256(input_file.read_bytes()).hexdigest() != fixed_hash:
                raise RuntimeError("ODAS modified frozen source WAV: " + scene_name)
        unchanged = (
            "8x8" in raw_ssl and "8x8_sst4" in raw_ssl
            and raw_ssl["8x8"] == raw_ssl["8x8_sst4"])
        if not unchanged:
            errors.append(scene_name + ": raw SSL changed after SST-only input cap")
        results.append({
            "scene": scene_name, "input_sha256": fixed_hash,
            "raw_ssl_unchanged_by_sst_limit": unchanged,
            "configurations": per_configuration,
        })
    report = {
        "schema": SCHEMA,
        "real_speech_from": "LibriSpeech SLR12 dev-clean, CC BY 4.0",
        "fixed_source_run_id": 42368354767,
        "same_input_wav_for_every_configuration": True,
        "capacity_configs": [
            {"id": name, "ssl": nssl, "sst": nsst, "sst_input_cap": cap}
            for name, nssl, nsst, cap in CONFIGS],
        "passed_integrity": not errors,
        "errors": errors,
        "results": results,
        "limitations": [
            "Directional one-to-one track association only, 45-degree diagnostic gate",
            "More tracks does not establish two independent separated waveforms",
            "No actual SSS leakage/SIRi/SI-SDRi or pinned ASR WER",
            "Recorded voices from one original dev-clean with utterance-heldout split",
        ],
    }
    (output / "capacity_comparison.json").write_text(
        json.dumps(report, indent=2) + "\n")
    return not errors


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not run_sweep(args.runner, args.source_root, args.output):
        raise SystemExit("ODAS real speech capacity sweep failed integrity checks")


if __name__ == "__main__":
    main()
