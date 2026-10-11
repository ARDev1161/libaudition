#!/usr/bin/env python3
"""Inspect YAMNet's per-patch event scores on full, weakly labelled ESC clips.

This deliberately compares temporal max and clip mean as *diagnostics*, not
as calibrated probabilities or a validated AudioSet accuracy metric.
YAMNet's native frame hop is 0.48 s and each frame sees ~0.96 s of audio.
"""
import argparse
import csv
import json
import subprocess
from pathlib import Path

import numpy as np

PATCH_HOP_SECONDS = 0.48
SAMPLE_RATE = 16000
CLASS_COUNT = 521


def parse_class_map(path):
    with path.open(newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != CLASS_COUNT:
        raise ValueError(f"Expected 521 YAMNet classes, got {len(rows)}")
    mids = []
    names = []
    for index, row in enumerate(rows):
        if int(row["index"]) != index or not row["mid"] or not row["display_name"]:
            raise ValueError(f"Invalid AudioSet class map at index {index}")
        mids.append(row["mid"])
        names.append(row["display_name"])
    if len(set(mids)) != CLASS_COUNT:
        raise ValueError("Duplicate YAMNet AudioSet MIDs")
    return mids, names


def summarise_scores(scores, names, truth_indices):
    """Pure numeric logic, separately regression-tested without any ONNX."""
    array = np.asarray(scores)
    if array.ndim != 2 or array.shape[1] != CLASS_COUNT or array.shape[0] == 0:
        raise ValueError("Expected a nonempty [patches,521] matrix")
    if not np.isfinite(array).all() or (array < 0).any() or (array > 1).any():
        raise ValueError("YAMNet scores must be finite probabilities [0,1]")
    mean = np.mean(array, axis=0, dtype=np.float64)
    peak = np.max(array, axis=0)
    mean_rank = np.argsort(-mean, kind="stable")
    peak_rank = np.argsort(-peak, kind="stable")
    def top5(rank, values):
        return [{"label": names[i], "score": float(values[i])}
                for i in rank[:5]]
    truth = []
    for idx in truth_indices:
        patch = int(np.argmax(array[:, idx]))
        truth.append({
            "label": names[idx],
            "mean_score": float(mean[idx]),
            "peak_score": float(peak[idx]),
            "peak_patch": patch,
            "approx_peak_start_s": round(patch * PATCH_HOP_SECONDS, 3),
            "mean_rank": int(np.flatnonzero(mean_rank == idx)[0]) + 1,
            "peak_rank": int(np.flatnonzero(peak_rank == idx)[0]) + 1,
        })
    patches = []
    for i, row in enumerate(array):
        ordered = np.argsort(-row, kind="stable")[:3]
        patches.append({
            "index": i, "approx_start_s": round(i * PATCH_HOP_SECONDS, 3),
            "top3": [{"label": names[j], "score": float(row[j])} for j in ordered],
        })
    return {
        "patches": int(array.shape[0]),
        "mean_top5": top5(mean_rank, mean),
        "peak_top5": top5(peak_rank, peak),
        "mean_hit5": bool(set(truth_indices) & set(mean_rank[:5])),
        "peak_hit5": bool(set(truth_indices) & set(peak_rank[:5])),
        "ground_truth": truth,
        "timeline": patches,
    }


def run():
    p = argparse.ArgumentParser()
    p.add_argument("--manifest", type=Path, required=True, help="CSV: wav,mid")
    p.add_argument("--model", type=Path, required=True)
    p.add_argument("--labels", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()

    import onnxruntime as ort

    mids, names = parse_class_map(args.labels)
    by_mid = {mid: i for i, mid in enumerate(mids)}
    session = ort.InferenceSession(str(args.model), providers=["CPUExecutionProvider"])
    ins = session.get_inputs()
    outs = session.get_outputs()
    if len(ins) != 1:
        raise ValueError("Expected a single waveform input")
    score_outputs = [o for o in outs if o.shape and o.shape[-1] == CLASS_COUNT]
    if not score_outputs:
        raise ValueError("Expected YAMNet 521-class scores")
    scores_output = next((o for o in score_outputs if "score" in o.name),
                         score_outputs[0])
    with args.manifest.open(newline="", encoding="utf-8") as stream:
        entries = list(csv.DictReader(stream))
    if not entries or not {"wav", "mid"} <= set(entries[0]):
        raise ValueError("Expected nonempty wav,mid manifest")

    clips = []
    for item in entries:
        wav_path = Path(item["wav"])
        if not wav_path.is_absolute():
            wav_path = args.manifest.resolve().parent / wav_path
        truth = [by_mid[mid.strip()] for mid in item["mid"].split("|")
                 if mid.strip() in by_mid]
        if not truth:
            raise ValueError(f"No AudioSet MID in YAMNet vocabulary: {item}")
        raw = subprocess.check_output([
            "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
            "-i", str(wav_path), "-ac", "1", "-ar", str(SAMPLE_RATE),
            "-f", "f32le", "-acodec", "pcm_f32le", "pipe:1",
        ])
        signal = np.frombuffer(raw, dtype="<f4").copy()
        if signal.size < SAMPLE_RATE:
            raise ValueError(f"Too short for YAMNet: {wav_path}")
        shape = ins[0].shape
        if len(shape) == 1:
            input_audio = signal
        elif len(shape) == 2:
            input_audio = signal.reshape(1, -1)
        else:
            raise ValueError(f"Unexpected YAMNet waveform shape {shape}")
        result = session.run([scores_output.name], {ins[0].name: input_audio})[0]
        if result.size % CLASS_COUNT:
            raise ValueError("Invalid YAMNet output shape")
        summary = summarise_scores(result.reshape(-1, CLASS_COUNT), names, truth)
        summary.update({
            "wav": item["wav"],
            "audio_seconds": round(float(signal.size / SAMPLE_RATE), 4),
        })
        clips.append(summary)
        print(f"{item['wav']}: {summary['patches']} patches, "
              f"mean hit@5={summary['mean_hit5']}, peak hit@5={summary['peak_hit5']}; "
              f"truth: {[(v['label'],v['peak_score'],v['approx_peak_start_s']) for v in summary['ground_truth']]}")
    report = {
        "source": "waveform-input YAMNet ONNX",
        "window_basis": "complete weakly annotated source clip",
        "sample_rate": SAMPLE_RATE,
        "approx_patch_hop_seconds": PATCH_HOP_SECONDS,
        "clip_count": len(clips),
        "mean_hit5_count": sum(c["mean_hit5"] for c in clips),
        "peak_hit5_count": sum(c["peak_hit5"] for c in clips),
        "warning": "Exploratory, weak clip-level labels; peak/max may select false positives and is not calibrated.",
        "clips": clips,
    }
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                           encoding="utf-8")
    print(f"Mean top5 hits {report['mean_hit5_count']}/{len(clips)}, "
          f"peak top5 hits {report['peak_hit5_count']}/{len(clips)}")


if __name__ == "__main__":
    run()
