#!/usr/bin/env python3
"""Independent, standard-library Acoustix PCM clock and activity oracle.

This checks the serialized render artifacts without importing the simulator,
ODAS, NumPy or SciPy. A source active at the nominal midpoint of a partially
filled last hop is not active when that midpoint is outside dry source frames.
"""
from __future__ import annotations

import math
from pathlib import Path
import wave


HOP_FRAMES = 128


def verify_timing(scene: dict, manifest: dict, wav_path: Path) -> dict:
    fs = scene["sample_rate_hz"]
    if fs != 16000 or manifest.get("sample_rate_hz") != fs:
        raise ValueError("Invalid or inconsistent sample clock")
    frames = manifest.get("simulated_frames")
    output_frames = manifest.get("wav_frames")
    if (type(frames) is not int or type(output_frames) is not int
            or frames <= 0 or frames > round(scene["duration_s"] * fs)
            or output_frames < frames or output_frames % HOP_FRAMES):
        raise ValueError("Invalid simulated/output frame counts")
    expected_output = math.ceil((frames + 3 * fs) / HOP_FRAMES) * HOP_FRAMES
    if output_frames != expected_output:
        raise ValueError("Expected exactly three seconds of padded RIR tail")
    with wave.open(str(wav_path), "rb") as reader:
        if (reader.getnchannels(), reader.getsampwidth(),
                reader.getframerate(), reader.getnframes(),
                reader.getcomptype()) != (6, 2, fs, output_frames, "NONE"):
            raise ValueError("WAV header disagrees with PCM clock contract")
        if len(reader.readframes(output_frames)) != output_frames * 6 * 2:
            raise ValueError("WAV PCM data is truncated")

    hops = manifest.get("ground_truth")
    if not isinstance(hops, list) or len(hops) != math.ceil(frames / HOP_FRAMES):
        raise ValueError("Incorrect number of geometric oracle hops")
    source_ids = [source["id"] for source in scene["sources"]]
    if len(set(source_ids)) != len(source_ids):
        raise ValueError("Duplicate input source identity")
    for index, item in enumerate(hops):
        midpoint_frame = index * HOP_FRAMES + HOP_FRAMES // 2
        t = midpoint_frame / fs
        if type(item.get("hop")) is not int or item["hop"] != index:
            raise ValueError("Geometric oracle hop ID is not contiguous")
        if not math.isfinite(item.get("t_s", float("nan"))) or abs(item["t_s"] - t) > 0.000051:
            raise ValueError("Geometric oracle timestamp differs from PCM clock")
        actual = [source["id"] for source in item["sources"]]
        if len(actual) != len(set(actual)):
            raise ValueError("Duplicated active source in geometric oracle")
        expected = [
            source["id"] for source in scene["sources"]
            if midpoint_frame < frames and any(
                start <= t < stop for start, stop in source["active_s"])
        ]
        if set(actual) != set(expected):
            raise ValueError(
                f"Active source disagreement at hop {index}: "
                f"expected {expected}, got {actual}")
    return {
        "sample_rate_hz": fs,
        "source_frames": frames,
        "wav_frames": output_frames,
        "oracle_hops": len(hops),
        "hop_frames": HOP_FRAMES,
        "partial_last_hop_frames": frames % HOP_FRAMES,
        "padding_frames": output_frames - frames,
    }
