#!/usr/bin/env python3
"""Controlled native ODAS GMM sensitivity on frozen real Acoustix S2 inputs.

Only the *offline test runner* accepts GMM overrides. No production changes.
T00 is explicitly SYNTHETIC ZERO-PCM and is not a measured room-silence WAV.
A passing sweep means native replays/provenance/trace checks passed, NOT that
two-voice separation or ASR improved.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import wave

from acoustix_s2_gmm_doa_audit import measure_t03_oracle_pairs, read_native_tracks


REAL_CASES = ("t02_dev_single", "t03_eval_two", "t07_eval_seven")
VARIANTS = {
    "baseline": (),
    "active_mean_030": ("--sst-active-mean", "0.30"),
    "active_var_001": ("--sst-active-variance", "0.01"),
    "false_prior_003": ("--sst-false-prior", "0.03"),
}
EXPECTED = {
    "baseline": {"active_mean": 0.4, "active_variance": 0.0025, "false_prior": 0.1},
    "active_mean_030": {"active_mean": 0.3, "active_variance": 0.0025, "false_prior": 0.1},
    "active_var_001": {"active_mean": 0.4, "active_variance": 0.01, "false_prior": 0.1},
    "false_prior_003": {"active_mean": 0.4, "active_variance": 0.0025, "false_prior": 0.03},
}


class SweepError(ValueError):
    pass


def file_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as src:
        for block in iter(lambda: src.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path):
    with Path(path).open(encoding="utf-8") as src:
        return json.load(src)


def silent_control(source_wav, target_wav):
    """Same PCM format/frame count, ALL channels zero. Not a real room take."""
    with wave.open(str(source_wav), "rb") as src:
        if (src.getnchannels() != 6 or src.getsampwidth() != 2
                or src.getframerate() != 16000 or src.getnframes() != 240000
                or src.getcomptype() != "NONE"):
            raise SweepError("T00 source must be frozen six-channel 16-bit 16kHz WAV")
        params = src.getparams()
    target_wav.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(target_wav), "wb") as out:
        out.setparams(params)
        out.writeframes(b"\0" * (params.nframes * params.nchannels * params.sampwidth))
    with wave.open(str(target_wav), "rb") as test:
        if test.getparams() != params or test.readframes(params.nframes).strip(b"\0"):
            raise SweepError("T00 zero-PCM integrity failure")
    return file_sha256(target_wav)


def oracle_overlap_frames(case_root):
    ground = read_json(case_root / "render/ground_truth.json")
    rows = ground["ground_truth"]
    # The frozen oracle can cover fewer hops than the full 15-second WAV;
    # do not invent speech/source labels in the trailing unlabelled period.
    if not rows or len(rows) > 1875:
        raise SweepError("empty or oversized Acoustix oracle")
    if any(row["hop"] != index for index, row in enumerate(rows)):
        raise SweepError("oracle hop discontinuity")
    return frozenset(index for index, row in enumerate(rows)
                     if sum(src.get("kind") == "voice" for src in row["sources"]) == 2)


def trace_metrics(rows, frames, overlap=None):
    """Counts actual ODAS state A, not human identity or separated audio."""
    if len(rows) * 128 != frames or frames != 240000:
        raise SweepError("real/frozen WAV hop count mismatch")
    if overlap is not None and (not overlap or max(overlap) >= len(rows)):
        raise SweepError("invalid actual speech overlap oracle")
    all_active_ids = set()
    active_hops = 0
    simultaneous = 0
    active_on_overlap = 0
    two_on_overlap = 0
    births = 0
    retirements = 0
    provisional = 0
    for i, row in enumerate(rows):
        if (row["schema"] != "odas_sst_raw_v1"
                or row["odas_timestamp"] != i + 1
                or row["hop_index"] != i
                or row["sample_offset"] != i * 128
                or row["hop_frames"] != 128):
            raise SweepError(f"noncontiguous ODAS trace at hop {i}")
        tracks = row["tracks_after"]
        if len(tracks) > 4:
            raise SweepError("ODAS exceeded max 4 track capacity")
        active = [track["track_id"] for track in tracks
                  if track["state"] == "A"]
        if len(set(active)) != len(active):
            raise SweepError("duplicate active ODAS track ID")
        all_active_ids.update(active)
        active_hops += bool(active)
        simultaneous = max(simultaneous, len(active))
        if overlap is not None and i in overlap:
            active_on_overlap += bool(active)
            two_on_overlap += len(active) >= 2
        births += len(row["births"])
        retirements += len(row["retirements"])
        provisional += sum(t["state"] == "P" for t in tracks)
    return {
        "validated_128_frame_hops": len(rows),
        "hops_any_active": active_hops,
        "max_simultaneous_active_tracks": simultaneous,
        "unique_active_sst_ids": len(all_active_ids),
        "birth_events": births,
        "retirement_events": retirements,
        "total_provisional_track_hops": provisional,
        "oracle_exactly_two_voice_hops": len(overlap) if overlap is not None else None,
        "oracle_two_voice_hops_with_any_active_track":
            active_on_overlap if overlap is not None else None,
        "oracle_two_voice_hops_with_at_least_two_active_tracks":
            two_on_overlap if overlap is not None else None,
    }


def verify_recorded_case(trace_path, summary_path, wav_path, expected_config,
                         expected_input_sha, oracle_overlap, trace_validator=None):
    if file_sha256(wav_path) != expected_input_sha:
        raise SweepError("not byte-identical input WAV across variants")
    summary = read_json(summary_path)
    if (summary["input_frames"] != 240000
            or summary["hops"] != 1875
            or summary["sample_rate_hz"] != 16000
            or summary["recorder_drops"] != 0
            or summary["sst_max_tracks"] != 4):
        raise SweepError("audio frames/hops/drops/track capacity contract failed")
    params = summary["sst_experiment"]
    for key, value in expected_config.items():
        if abs(params[key] - value) > 0.0000001:
            raise SweepError(f"runtime SST parameter {key} did not match experiment")
    if (params["new_prior"] != 0.1 or params["track_prior"] != 0.8
            or params["birth_threshold"] != 0.9
            or params["probation_window"] != 5
            or params["probation_threshold"] != 0.8):
        raise SweepError("non-experimental SST parameter changed unexpectedly")
    if trace_validator:
        subprocess.run([sys.executable, str(trace_validator), str(trace_path)],
                       check=True, timeout=30, capture_output=True, text=True)
    rows = []
    with Path(trace_path).open(encoding="utf-8") as src:
        for line in src:
            rows.append(json.loads(line))
    if len(rows) > 1875:
        raise SweepError("trace exceeds recorded PCM duration")
    return trace_metrics(rows, summary["input_frames"], oracle_overlap)


def run_one(runner, validator, wav, output, args, sha, overlap, expected):
    output.mkdir(parents=True, exist_ok=False)
    trace = output / "sst_raw_trace.jsonl"
    env = dict(os.environ)
    env["ODAS_SST_TRACE_JSONL"] = str(trace.resolve())
    env["ODAS_SST_TRACE_HOP_FRAMES"] = "128"
    command = [str(runner.resolve()), str(wav.resolve()),
               str(output.resolve()), *args]
    with (output / "runner.stdout.txt").open("w", encoding="utf-8") as log,\
         (output / "runner.stderr.txt").open("w", encoding="utf-8") as err:
        subprocess.run(command, check=True, env=env, stdout=log, stderr=err,
                       timeout=180)
    if not trace.is_file() or trace.stat().st_size == 0:
        raise SweepError(f"missing native ODAS trace at {output}")
    return verify_recorded_case(
        trace, output / "summary.json", wav, expected, sha, overlap, validator)


def sweep(root, runner, validator, output):
    root = Path(root)
    runner, validator = Path(runner), Path(validator)
    output = Path(output)
    if output.exists():
        raise SweepError("experiment output already exists; refuse stale append")
    if not runner.is_file() or not validator.is_file():
        raise SweepError("missing real native runner or exact ODAS validator")
    source_wavs = {}
    overlap = {}
    for name in REAL_CASES:
        case = root / name
        wav = case / "render/input.wav"
        digest = file_sha256(wav)
        expected = (case / "input_sha256.txt").read_text().strip()
        if digest != expected:
            raise SweepError(f"{name} source WAV SHA does not match frozen manifest")
        source_wavs[name] = (wav, digest)
        if name == "t03_eval_two":
            overlap[name] = oracle_overlap_frames(case)
    output.mkdir(parents=True, exist_ok=False)
    t00 = output / "t00_synthetic_zero_pcm.wav"
    t00_sha = silent_control(source_wavs["t02_dev_single"][0], t00)
    source_wavs["t00_zero_pcm_control"] = (t00, t00_sha)
    report = {
        "schema": "libaudition.s2_native_gmm_sensitivity.v1",
        "source": "frozen genuine Acoustix T02/T03/T07 PCM; no rerender",
        "t00_origin": "SYNTHETIC ZERO-PCM control made from frozen T02 WAV format, NOT a recorded or rendered ambient scene",
        "claim": "structural replay and track-count diagnostics only; no SI-SDRi, SIRi, WER or human identity",
        "variants": {},
    }
    for variant, args in VARIANTS.items():
        entry = {}
        for case_name, (wav, sha) in source_wavs.items():
            dest = output / variant / case_name
            if variant == "baseline" and case_name in REAL_CASES:
                original = root / case_name / "odas"
                metrics = verify_recorded_case(
                    original / "sst_raw_trace.jsonl",
                    original / "summary.json", wav, EXPECTED[variant],
                    sha, overlap.get(case_name), validator)
                trace_sha = file_sha256(original / "sst_raw_trace.jsonl")
                source_kind = "already replayed native baseline in same CI"
            else:
                metrics = run_one(runner, validator, wav, dest, args, sha,
                                  overlap.get(case_name), EXPECTED[variant])
                trace_sha = file_sha256(dest / "sst_raw_trace.jsonl")
                source_kind = "fresh native ODAS replay"
            case_entry = {
                "wav_sha256": sha, "trace_sha256": trace_sha,
                "source": source_kind, "metrics": metrics,
            }
            if case_name == "t03_eval_two":
                native_dir = (root / case_name / "odas"
                              if variant == "baseline" else dest)
                true_oracle = read_json(
                    root / case_name / "render/ground_truth.json")["ground_truth"]
                spatial = measure_t03_oracle_pairs(
                    true_oracle, read_native_tracks(
                        native_dir / "observations.csv"))
                if (spatial["oracle_exactly_two_voice_hops"] !=
                        metrics["oracle_exactly_two_voice_hops"] or
                    spatial["two_or_more_active_tracks_in_overlap_hops"] !=
                        metrics["oracle_two_voice_hops_with_at_least_two_active_tracks"]):
                    raise SweepError("active SST clock disagrees with native geometry observations")
                case_entry["spatial_oracle_diagnostics"] = spatial
            entry[case_name] = case_entry
        report["variants"][variant] = {
            "cli": list(args), "verified_parameters": EXPECTED[variant],
            "cases": entry,
        }
        print(json.dumps({"variant": variant,
                          "t00_any_active_hops": entry["t00_zero_pcm_control"]["metrics"]["hops_any_active"],
                          "t02_max_active": entry["t02_dev_single"]["metrics"]["max_simultaneous_active_tracks"],
                          "t03_two_active_overlap_hops": entry["t03_eval_two"]["metrics"]["oracle_two_voice_hops_with_at_least_two_active_tracks"],
                          "t07_max_active": entry["t07_eval_seven"]["metrics"]["max_simultaneous_active_tracks"]},
                         sort_keys=True), flush=True)
    (output / "gmm_sweep_report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--artifact-root", type=Path, required=True)
    ap.add_argument("--runner", type=Path, required=True)
    ap.add_argument("--validator", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    a = ap.parse_args()
    try:
        sweep(a.artifact_root, a.runner, a.validator, a.output)
    except (OSError, ValueError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as exc:
        ap.exit(2, f"BLOCKED: {exc}\n")


if __name__ == "__main__":
    main()
