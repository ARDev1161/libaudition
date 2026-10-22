#!/usr/bin/env python3
"""Behavioral checks on the pinned YAMNet waveform-input ONNX model.

Replicates the three synthetic waveforms and expected Top-10 classes from
tensorflow/models research/audioset/yamnet/yamnet_test.py (3 seconds, 16kHz).
This tests recognizer behavior rather than merely C++/ONNX adapter parity.
"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    with args.labels.open(newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if len(rows) != 521 or any(int(row["index"]) != i for i, row in enumerate(rows)):
        raise ValueError("Invalid or reordered 521-class YAMNet label map")
    names = [row["display_name"] for row in rows]
    samples = 3 * 16000
    waveform_tests = [
        ("zeros", np.zeros(samples, dtype=np.float32), "Silence"),
        ("white_noise", np.random.RandomState(51773).uniform(-1, 1, samples).astype(np.float32),
         "White noise"),
        ("sine_440hz", np.sin(2 * np.pi * 440 * np.arange(samples) / 16000).astype(np.float32),
         "Sine wave"),
    ]

    session = ort.InferenceSession(str(args.model), providers=["CPUExecutionProvider"])
    input_info = session.get_inputs()[0]
    if len(input_info.shape) not in (1, 2):
        raise ValueError(f"Expected raw mono waveform input, got {input_info.shape}")
    candidates = [o for o in session.get_outputs()
                  if o.shape and o.shape[-1] == 521]
    if not candidates:
        raise ValueError("YAMNet scores output not found")
    output = next((o for o in candidates if "score" in o.name), candidates[0])
    report = {"source": "tensorflow/models/research/audioset/yamnet/yamnet_test.py",
              "clip_seconds": 3, "tests": []}
    failed = []
    for key, signal, expected_label in waveform_tests:
        signal_in = signal.reshape(1, -1) if len(input_info.shape) == 2 else signal
        scores = session.run([output.name], {input_info.name: signal_in})[0]
        if scores.size % 521 or not np.all(np.isfinite(scores)):
            raise ValueError(f"{key}: invalid model score tensor")
        scores = scores.reshape(-1, 521)
        if np.any(scores < 0.0) or np.any(scores > 1.0):
            raise ValueError(f"{key}: expected probabilities in [0,1]")
        average = scores.mean(axis=0, dtype=np.float64)
        indices = np.argsort(-average, kind="stable")[:10]
        top10 = [{"label": names[int(i)], "score": float(average[i])}
                 for i in indices]
        passed = expected_label in {e["label"] for e in top10}
        report["tests"].append({"signal": key, "expected_in_top10": expected_label,
                                "passed": passed, "top10": top10,
                                "patch_count": int(scores.shape[0])})
        if not passed:
            failed.append(key)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if failed:
        raise SystemExit(f"FAIL: pinned YAMNet deviates from official behavioral smoke: {failed}")


if __name__ == "__main__":
    main()
