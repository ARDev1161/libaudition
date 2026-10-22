#!/usr/bin/env python3
"""Compare original waveform-input YAMNet ONNX scores against C++ adapter.

Validates raw per-patch score averaging and CSV label order, without relying
on a small audio classification dataset's noisy ground-truth annotations.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess

import numpy as np
import onnxruntime as ort


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--pcm16", type=Path, required=True)
    parser.add_argument("--cpp-probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    pcm = np.fromfile(args.pcm16, dtype="<i2")
    if pcm.size < 15600:
        raise ValueError("Expected >=15,600 input PCM samples at 16kHz")
    signal = pcm.astype(np.float32) / 32768.0
    session = ort.InferenceSession(str(args.model), providers=["CPUExecutionProvider"])
    input_info = session.get_inputs()[0]
    if len(input_info.shape) not in (1, 2):
        raise ValueError(f"Unexpected waveform input: {input_info.shape}")
    signal_in = signal.reshape(1, -1) if len(input_info.shape) == 2 else signal
    score_outputs = [o for o in session.get_outputs()
                     if o.shape and o.shape[-1] == 521]
    if not score_outputs:
        raise ValueError("Expected 521-class scores output")
    score_output = next((o for o in score_outputs if "score" in o.name), score_outputs[0])
    scores = session.run([score_output.name], {input_info.name: signal_in})[0]
    if scores.size % 521 or not np.all(np.isfinite(scores)):
        raise ValueError("Invalid raw ONNX scores")
    scores = scores.reshape(-1, 521)
    if np.any(scores < 0) or np.any(scores > 1):
        raise ValueError("Expected probabilities rather than logits")
    means = scores.mean(axis=0, dtype=np.float64)
    with args.labels.open(newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if len(rows) != 521 or any(int(row["index"]) != i for i, row in enumerate(rows)):
        raise ValueError("Invalid AudioSet CSV label order")
    names = [row["display_name"] for row in rows]
    expected = [(names[int(i)], float(means[i]))
                for i in np.argsort(-means, kind="stable")[:10]]
    result = subprocess.run([
        str(args.cpp_probe.resolve()), str(args.model.resolve()),
        str(args.labels.resolve()), str(args.pcm16.resolve())],
        check=True, text=True, capture_output=True)
    cpp = [line.rsplit("\t", 1) for line in result.stdout.splitlines()]
    if len(cpp) != len(expected):
        raise ValueError(f"Expected 10 C++ scores, got {result.stdout}")
    delta = 0.0
    for (label, score), (name, actual) in zip(expected, cpp):
        actual = float(actual)
        delta = max(delta, abs(score - actual))
        if name != label or abs(score - actual) > 2e-5:
            raise RuntimeError(f"YAMNet C++/ONNX mismatch: {label!r} {score} != {name!r} {actual}")
    report = {
        "pcm16_samples": int(pcm.size),
        "patches": int(scores.shape[0]),
        "max_abs_score_error": delta,
        "top10": [{"label": n, "score": s} for n, s in expected]
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
