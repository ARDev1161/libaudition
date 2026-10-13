#!/usr/bin/env python3
"""Deterministic, lossless, six-channel ReSpeaker-layout ODAS fixtures.

Only Python stdlib. Four virtual microphones use far-field fractional
time-of-arrival delays and the same geometry as tests/odas_backend_test.cpp.
Channel 0 is a convenience reference; 1..4 are microphones; 5 is playback
reference (silent). Directions are source bearings in the microphone frame.
"""
import argparse
import hashlib
import json
import math
import struct
import wave
from pathlib import Path

FS = 16000
C = 343.0
MICS = [(-0.032, 0.0), (0.0, -0.032),
        (0.032, 0.0), (0.0, 0.032)]
SCENES = {
    "silence": {"duration": 3.0, "sources": []},
    "static": {"duration": 5.0, "sources": [
        {"id": "a", "start_s": 0.5, "end_s": 4.5,
         "azimuth_start_deg": 30.0, "azimuth_end_deg": 30.0}]},
    "moving": {"duration": 6.0, "sources": [
        {"id": "a", "start_s": 0.5, "end_s": 5.5,
         "azimuth_start_deg": -80.0, "azimuth_end_deg": 80.0}]},
    "two": {"duration": 6.0, "sources": [
        {"id": "a", "start_s": 0.5, "end_s": 5.0,
         "azimuth_start_deg": -65.0, "azimuth_end_deg": -65.0},
        {"id": "b", "start_s": 1.5, "end_s": 5.5,
         "azimuth_start_deg": 70.0, "azimuth_end_deg": 70.0}]},
}


def signal(source, t):
    # Deterministic broadband voiced-like excitation; continuous in time,
    # therefore shifting t implements fractional-sample TDOA without aliasing
    # from integer rounding. Not a room-impulse-response simulator.
    if t < source["start_s"] or t >= source["end_s"]:
        return 0.0
    local = t - source["start_s"]
    edge = min(1.0, local / 0.05,
               (source["end_s"] - t) / 0.05)
    seed = 0 if source["id"] == "a" else 1
    value = 0.0
    for k in range(1, 11):
        frequency = (205.0 + 173.0 * k + 47.0 * (k % 3)
                     + seed * 89.0)
        phase = 2.0 * math.pi * (
            frequency * local + (18.0 + seed * 7.0) * local * local
        ) + k * (0.31 + seed * 0.13)
        value += math.sin(phase) / math.sqrt(k)
    return 0.095 * edge * value


def bearing(source, t):
    position = (t - source["start_s"]) / (
        source["end_s"] - source["start_s"])
    return source["azimuth_start_deg"] + position * (
        source["azimuth_end_deg"] - source["azimuth_start_deg"])


def make(outdir, name):
    scene = SCENES[name]
    path = outdir / (name + ".wav")
    sample_count = round(scene["duration"] * FS)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(6)
        output.setsampwidth(2)
        output.setframerate(FS)
        pcm = bytearray()
        for n in range(sample_count):
            t = n / FS
            mix = 0.0
            microphones = [0.0] * 4
            for source in scene["sources"]:
                azimuth = math.radians(bearing(source, t))
                dx, dy = math.cos(azimuth), math.sin(azimuth)
                mix += signal(source, t)
                for index, (x, y) in enumerate(MICS):
                    # Source bearing points FROM array TO source. A mic
                    # nearer the source receives the wave earlier.
                    arrival_advance = (x * dx + y * dy) / C
                    microphones[index] += signal(
                        source, t + arrival_advance)
            values = [mix] + microphones + [0.0]
            for value in values:
                pcm += struct.pack(
                    "<h", max(-32768, min(32767, round(value * 32767))))
            if len(pcm) >= 65536:
                output.writeframesraw(pcm)
                pcm.clear()
        output.writeframes(pcm)
    return {
        "file": path.name,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "frames": sample_count,
        "sources": scene["sources"],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema": "libaudition.spatial-fixture.v1",
        "sample_rate_hz": FS,
        "channels": 6,
        "microphone_channels": [1, 2, 3, 4],
        "microphones_m": MICS,
        "speed_of_sound_mps": C,
        "azimuth_convention": "atan2(y,x), degrees, x-right/y-up",
        "generator": "analytic-far-field-v1",
        "scenes": {},
    }
    for name in SCENES:
        manifest["scenes"][name] = make(args.output, name)
    (args.output / "ground_truth.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    print(f"generated {len(SCENES)} deterministic scenes in {args.output}")


if __name__ == "__main__":
    main()
