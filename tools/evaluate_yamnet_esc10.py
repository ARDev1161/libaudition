#!/usr/bin/env python3
"""Evaluate waveform YAMNet on 5s ESC-10 clips vs the (possibly silent) first 1s.

Only source-clip labels are known. This is a four-class exploratory diagnostic
and does not estimate AudioSet mAP or real-world false-positive rates.
"""
import argparse
import csv
import json
from pathlib import Path
import statistics
import subprocess
import time

import numpy as np

SAMPLE_RATE = 16000
NUM_CLASSES = 521


def load_labels(path):
    with path.open(newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != NUM_CLASSES or any(int(r["index"]) != i for i, r in enumerate(rows)):
        raise ValueError("Expected ordered 521-class YAMNet CSV")
    mids = [r["mid"].strip() for r in rows]
    labels = [r["display_name"].strip() for r in rows]
    if any(not mid or not name for mid, name in zip(mids, labels)) or len(set(mids)) != NUM_CLASSES:
        raise ValueError("Missing or duplicated AudioSet MID")
    return mids, labels


def compute_ranks(patch_scores):
    """Return native YAMNet clip means and diagnostic (uncalibrated) peaks."""
    scores = np.asarray(patch_scores)
    if scores.ndim != 2 or scores.shape[0] == 0 or scores.shape[1] != NUM_CLASSES:
        raise ValueError("Expected nonempty YAMNet score tensor [patches,521]")
    if not np.isfinite(scores).all() or (scores < 0).any() or (scores > 1).any():
        raise ValueError("Invalid YAMNet scores outside [0,1]")
    mean = scores.mean(axis=0, dtype=np.float64)
    peak = scores.max(axis=0)
    return {"mean": np.argsort(-mean, kind="stable"),
            "peak": np.argsort(-peak, kind="stable")}, {"mean": mean, "peak": peak}


def summarize_records(records, class_mids):
    """Micro-averaged and equally class-weighted Top-1/Top-5 recall."""
    if not records or not class_mids:
        raise ValueError("No evaluation observations")
    by_mid = {mid: [] for mid in class_mids}
    for record in records:
        if record["truth_mid"] not in by_mid:
            raise ValueError("Unexpected evaluation class")
        by_mid[record["truth_mid"]].append(record)
    if any(not examples for examples in by_mid.values()):
        raise ValueError("Some evaluation classes have no examples")
    result = {}
    for mode in ("first_second", "whole_clip_mean", "whole_clip_peak"):
        per_class = {}
        for mid, examples in by_mid.items():
            n = len(examples)
            per_class[mid] = {
                "clips": n,
                "top1": sum(x[mode]["top1_hit"] for x in examples) / n,
                "top5": sum(x[mode]["top5_hit"] for x in examples) / n,
            }
        result[mode] = {
            "top1_micro": sum(x[mode]["top1_hit"] for x in records) / len(records),
            "top5_micro": sum(x[mode]["top5_hit"] for x in records) / len(records),
            "top1_macro": statistics.mean(x["top1"] for x in per_class.values()),
            "top5_macro": statistics.mean(x["top5"] for x in per_class.values()),
            "per_class": per_class,
        }
    return result


def decode_wav_16k(path):
    raw = subprocess.check_output([
        "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
        "-i", str(path), "-ac", "1", "-ar", str(SAMPLE_RATE),
        "-f", "f32le", "-acodec", "pcm_f32le", "pipe:1",
    ])
    pcm = np.frombuffer(raw, dtype="<f4").copy()
    if pcm.size < 5 * SAMPLE_RATE or pcm.size > 5 * SAMPLE_RATE + 160:
        raise ValueError(f"Expected ~5s ESC-10 recording, got {pcm.size} samples: {path}")
    return pcm[:5 * SAMPLE_RATE]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-clips", type=int, default=0)
    args = parser.parse_args()
    import onnxruntime as ort

    mids, labels = load_labels(args.labels)
    by_mid = {mid: i for i, mid in enumerate(mids)}
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    session = ort.InferenceSession(str(args.model), options,
                                   providers=["CPUExecutionProvider"])
    if len(session.get_inputs()) != 1:
        raise ValueError("Expected one YAMNet waveform input")
    input_info = session.get_inputs()[0]
    shape = input_info.shape
    if len(shape) not in (1, 2):
        raise ValueError(f"Unexpected YAMNet input shape {shape}")
    outputs = [o for o in session.get_outputs() if o.shape and o.shape[-1] == NUM_CLASSES]
    if not outputs:
        raise ValueError("YAMNet scores output not found")
    score_output = next((o for o in outputs if "score" in o.name), outputs[0])

    def infer(audio):
        waveform = audio.reshape(1, -1) if len(shape) == 2 else audio
        start = time.perf_counter()
        pred = session.run([score_output.name], {input_info.name: waveform})[0]
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        if pred.size % NUM_CLASSES:
            raise ValueError("Invalid YAMNet output shape")
        ranks, values = compute_ranks(pred.reshape(-1, NUM_CLASSES))
        return ranks, values, elapsed_ms, pred.size // NUM_CLASSES

    with args.manifest.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows or not {"wav", "mid"} <= rows[0].keys():
        raise ValueError("Expected nonempty wav,mid manifest")
    if args.expected_clips and len(rows) != args.expected_clips:
        raise ValueError(f"Expected {args.expected_clips} clips, got {len(rows)}")
    if len({r["wav"] for r in rows}) != len(rows):
        raise ValueError("Duplicate WAV names in manifest")

    observations = []
    latencies = {"first_second": [], "whole_clip": []}
    for count, row in enumerate(rows, 1):
        mid = row["mid"].strip()
        if "|" in mid or mid not in by_mid:
            raise ValueError(f"Expected one known AudioSet MID per clip: {row}")
        truth = by_mid[mid]
        wav = Path(row["wav"])
        if not wav.is_absolute():
            wav = args.manifest.resolve().parent / wav
        samples = decode_wav_16k(wav)
        ranks1, scores1, elapsed1, patches1 = infer(samples[:SAMPLE_RATE])
        ranks5, scores5, elapsed5, patches5 = infer(samples)
        latencies["first_second"].append(elapsed1)
        latencies["whole_clip"].append(elapsed5)

        def prediction(indices, scores):
            top = [int(i) for i in indices[:5]]
            return {
                "top1_hit": bool(top[0] == truth),
                "top5_hit": bool(truth in top),
                "truth_rank": int(np.flatnonzero(indices == truth)[0]) + 1,
                "truth_score": float(scores[truth]),
                "top5": [{"mid": mids[i], "label": labels[i], "score": float(scores[i])}
                         for i in top],
            }
        observations.append({
            "wav": row["wav"], "truth_mid": mid, "truth_label": labels[truth],
            "first_patches": int(patches1), "whole_patches": int(patches5),
            "first_second": prediction(ranks1["mean"], scores1["mean"]),
            "whole_clip_mean": prediction(ranks5["mean"], scores5["mean"]),
            "whole_clip_peak": prediction(ranks5["peak"], scores5["peak"]),
        })
        if count % 10 == 0:
            print(f"Evaluated {count}/{len(rows)} ESC-10 clips", flush=True)

    used_mids = sorted({o["truth_mid"] for o in observations})
    report = {
        "clips": len(observations),
        "classes": [{"mid": mid, "label": labels[by_mid[mid]],
                     "clips": sum(o["truth_mid"] == mid for o in observations)}
                    for mid in used_mids],
        "model": "YAMNet waveform ONNX, CPUExecutionProvider",
        "sample_rate_hz": SAMPLE_RATE,
        "limitations": [
            "ESC-10 labels cover whole 5-second clips, not necessarily first-second excerpts.",
            "Only four ESC-10 classes; metrics are not AudioSet mAP.",
            "Temporal peak is uncalibrated; it may increase false positives.",
            "Inference-only CI timings exclude WAV decoding and model initialization.",
        ],
        "metrics": summarize_records(observations, used_mids),
        "latency_ms": {name: {"median": statistics.median(times),
                              "p95": sorted(times)[int(0.95 * (len(times) - 1))]}
                       for name, times in latencies.items()},
        "observations": observations,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                           encoding="utf-8")
    for mode, values in report["metrics"].items():
        print(f"{mode}: n={len(observations)} top1={values['top1_micro']:.3f} "
              f"top5={values['top5_micro']:.3f} macro_top5={values['top5_macro']:.3f}")


if __name__ == "__main__":
    main()
