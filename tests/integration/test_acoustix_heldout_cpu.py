#!/usr/bin/env python3
"""Actual pinned Acoustix CPU render audit for frozen S1 evaluation cases.

This script renders independent WAV/PCM source stems with the SAME production
Acoustix room renderer as the 12-second scene, not an analytical RIR mock.
Each selected case is rendered twice on the reference runner and must produce
identical per-artifact SHA-256 hashes. An evidence JSON is always written,
including failures; unrendered cases are explicitly not accepted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace

from acoustix_heldout_manifest import SCHEMA, entries, verify_manifest
from acoustix_signal_contract import audit_audio
from acoustix_timing_contract import verify_timing
from test_acoustix_replay import (
    encoded_scene, artifact_signatures, make_scene,
)


def build_case(item: dict) -> dict:
    scene = make_scene(item["recipe"])
    scene["room"]["rt60_s"] = item["requested_rt60_s"]
    scene["description"] = ("S1 frozen held-out physics evaluation; "
                            "not S2 actual speech: " + item["recipe"])
    digest = hashlib.sha256(encoded_scene(scene)).hexdigest()
    if digest != item["scene_sha256"]:
        raise ValueError("Frozen S1 scene hash mismatch for " + item["case_id"])
    return scene


def run_case(item: dict, root: Path) -> dict:
    from acoustix_scene import render

    scene = build_case(item)
    folder = root / item["case_id"]
    folder.mkdir(parents=True, exist_ok=True)
    scene_file = folder / "scene.json"
    scene_file.write_bytes(encoded_scene(scene))
    signatures = []
    observations = []
    ids = [src["id"] for src in scene["sources"]]
    for attempt in (1, 2):
        output = folder / ("render_" + str(attempt))
        render(scene, SimpleNamespace(
            scene=scene_file, output=output,
            seed=item["seed"], hop_ms=250,
            max_seconds=None, speech_dir=None, stems=True, runner=None,
            compare_capacity=False, uncalibrated_rt60=False))
        gt = json.loads((output / "ground_truth.json").read_text())
        audit = audit_audio(output / "input.wav", output / "stems", ids)
        timing = verify_timing(scene, gt, output / "input.wav")
        if (gt["scene_sha256"] != item["scene_sha256"]
                or gt["render_seed"] != item["seed"]
                or gt["rir_hop_ms"] != 250
                or gt["source_count"] != len(ids)
                or gt["mic_count"] != 4):
            raise ValueError("Acoustix runtime provenance/geometry mismatch")
        if audit["reconstruction_max_residual_lsb"] > audit["quantization_allowance_lsb"]:
            raise ValueError("PCM source stems do not reconstruct the microphone WAV")
        if audit["post_acoustic_signal_silence_frames"] <= 0:
            raise ValueError("Acoustic RIR tail exhausted output duration")
        signatures.append(artifact_signatures(output, ids))
        observations.append({
            "timing": timing,
            "pcm_max_residual_lsb": audit["reconstruction_max_residual_lsb"],
            "rt60_calibration": json.loads(
                (output / "rt60_calibration.json").read_text()),
        })
    if signatures[0] != signatures[1]:
        difference = sorted(
            name for name in signatures[0]
            if signatures[0][name] != signatures[1][name])
        raise AssertionError("Nondeterministic Acoustix artifacts: " + ", ".join(difference))
    return {
        "case_id": item["case_id"],
        "seed": item["seed"],
        "recipe": item["recipe"],
        "requested_rt60_s": item["requested_rt60_s"],
        "scene_sha256": item["scene_sha256"],
        "two_independent_cpu_renders_equal": True,
        "artifact_sha256": signatures[0],
        "observations": observations,
        "passed": True,
    }


def run_slice(start: int, count: int, root: Path, report_path: Path) -> bool:
    cases = entries()
    verify_manifest(cases)
    if start < 0 or count <= 0 or start + count > len(cases):
        raise ValueError("Invalid frozen S1 evaluation slice")
    root.mkdir(parents=True, exist_ok=True)
    results = []
    failures = 0
    try:
        for item in cases[start:start + count]:
            try:
                result = run_case(item, root)
            except Exception as exc:
                failures += 1
                result = {
                    "case_id": item["case_id"], "seed": item["seed"],
                    "passed": False, "error_type": type(exc).__name__,
                    "error": str(exc),
                }
            results.append(result)
            print(json.dumps({
                "case_id": result["case_id"], "passed": result["passed"],
                "error": result.get("error"),
            }), flush=True)
    finally:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps({
            "schema": "libaudition.acoustix-heldout-cpu.v1",
            "manifest_schema": SCHEMA,
            "status": "PASSED_SELECTED_SLICE" if failures == 0 else "FAILED_SELECTED_SLICE",
            "full_twenty_case_qualification": start == 0 and count == 20 and failures == 0,
            "selected_case_count": count, "selected_start_index": start,
            "executed_case_count": len(results),
            "passed": failures == 0 and len(results) == count,
            "cases": results,
            "limitations": [
                "Only reference-runner byte reproducibility; no cross-platform guarantee",
                "Synthetic excitations are not real human speech",
                "No SNR controls or measured real-room impulse response",
                "All 20 must pass before full S1 held-out CPU render qualification"
            ],
        }, indent=2) + "\n")
    return failures == 0 and len(results) == count


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--start", type=int, default=0)
    parser.add_argument("--count", type=int, default=1)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if not run_slice(args.start, args.count, args.output, args.report):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
