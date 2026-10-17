#!/usr/bin/env python3
"""Prepare a static LOCATA development recording for independent DOA scoring.

Read original OptiTrack poses/timestamps, transform into array-local coordinates,
extract a short lossless PCM16 16 kHz subclip with ffmpeg, and import it via the
geometry-neutral libaudition real recording corpus importer.

Only static array AND static source are supported. Reject moving geometry and
uncertain synchronization instead of silently fabricating ground truth.
"""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timedelta
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import tempfile
import wave

from import_respeaker_recording import import_recording

FS = 16000
HOP = 128
SOURCE_LIMIT_M = 0.025
ARRAY_LIMIT_M = 0.015
ROTATION_LIMIT = 0.015
ENDPOINT_MARGIN_S = 0.15


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def locata_rows(path: Path):
    """LOCATA *.txt header is comma/tab/whitespace separated."""
    with path.open(encoding="utf-8-sig", newline="") as stream:
        head = stream.readline().strip()
        require(bool(head), f"Empty LOCATA table: {path}")
        if "\t" in head:
            delimiter = "\t"
        elif "," in head:
            delimiter = ","
        else:
            delimiter = None
        names = (head.split(delimiter) if delimiter else head.split())
        require(len(names) == len(set(names)), "Duplicate LOCATA column")
        for line in stream:
            if not line.strip():
                continue
            fields = line.strip().split(delimiter) if delimiter else line.split()
            require(len(fields) == len(names),
                    f"Malformed LOCATA row in {path.name}")
            yield dict(zip(names, fields))


def time_value(row):
    try:
        y, m, d, h, minute = (int(row[name]) for name in
                              ("year", "month", "day", "hour", "minute"))
        seconds = float(row["second"])
        require(math.isfinite(seconds) and 0 <= seconds < 60,
                "Invalid LOCATA seconds")
        date = datetime(y, m, d, h, minute) + timedelta(seconds=seconds)
        # Naive UTC-like comparison avoids the host machine's local timezone.
        return date.toordinal() * 86400.0 + (date - date.replace(
            hour=0, minute=0, second=0, microsecond=0)).total_seconds()
    except (ValueError, KeyError, TypeError) as exc:
        raise ValueError("Invalid LOCATA timestamp") from exc


def pose(row):
    def get(name):
        try:
            v = float(row[name])
        except (ValueError, TypeError, KeyError) as exc:
            raise ValueError("Missing LOCATA pose column: " + name) from exc
        require(math.isfinite(v), "Nonfinite LOCATA pose value")
        return v
    position = tuple(get(k) for k in ("x", "y", "z"))
    rotation = tuple(tuple(get(f"rotation_{i}{j}")
                     for j in (1, 2, 3)) for i in (1, 2, 3))
    # LOCATA's official get_truth.m uses R^T * (world - array_position).
    for col in range(3):
        length = sum(rotation[row][col] ** 2 for row in range(3))
        require(abs(length - 1.0) < 0.02,
                "Invalid LOCATA array rotation norm")
        for other in range(col):
            dot = sum(rotation[row][col] * rotation[row][other]
                      for row in range(3))
            require(abs(dot) < 0.02, "Non-orthogonal LOCATA rotation")
    return position, rotation


def local_vector(rotation, vector):
    return tuple(sum(rotation[j][i] * vector[j] for j in range(3))
                 for i in range(3))


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def take_static_poses(table, from_time, to_time, label):
    rows = []
    for row in locata_rows(table):
        timestamp = time_value(row)
        if from_time - ENDPOINT_MARGIN_S <= timestamp <= to_time + ENDPOINT_MARGIN_S:
            rows.append((timestamp, row))
    require(len(rows) >= 2, f"Too few LOCATA poses: {label}")
    require(min(t for t, _ in rows) <= from_time + ENDPOINT_MARGIN_S and
            max(t for t, _ in rows) >= to_time - ENDPOINT_MARGIN_S,
            f"LOCATA poses do not cover audio segment: {label}")
    require(all(rows[i][0] < rows[i + 1][0] for i in range(len(rows) - 1)),
            f"Non-monotone pose timestamps: {label}")
    return rows


def static_reference(array_rows, source_rows):
    """Compute mic-local geometry + full 3D source bearing from OptiTrack."""
    first_pos, first_rot = pose(array_rows[0][1])
    array_samples = []
    for _, row in array_rows:
        pos, rot = pose(row)
        require(distance(pos, first_pos) <= ARRAY_LIMIT_M,
                "LOCATA array is moving: static geometry unsupported")
        require(max(abs(rot[i][j] - first_rot[i][j])
                    for i in range(3) for j in range(3)) <= ROTATION_LIMIT,
                "LOCATA array is rotating: static geometry unsupported")
        array_samples.append((pos, rot, row))
    first_source_pos, _ = pose(source_rows[0][1])
    for _, row in source_rows:
        position, _ = pose(row)
        require(distance(position, first_source_pos) <= SOURCE_LIMIT_M,
                "LOCATA source is moving: use a time-varying ground-truth evaluator")
    # Position columns mic1_x/y/z etc are WORLD coordinates in the LOCATA
    # OptiTrack position table; transform them to the array's own frame.
    candidates = sorted(int(match.group(1)) for key in array_rows[0][1]
                        if (match := re.fullmatch(r"mic(\d+)_x", key)))
    require(candidates and candidates == list(range(1, len(candidates) + 1)),
            "LOCATA mic indices must be contiguous and start at 1")
    microphones = []
    for mic_id in candidates:
        coord_name = [f"mic{mic_id}_{axis}" for axis in "xyz"]
        positions = []
        for array_pos, rotation, row in array_samples:
            xyz = []
            for key in coord_name:
                try:
                    value = float(row[key])
                except (ValueError, TypeError, KeyError) as exc:
                    raise ValueError("Invalid LOCATA microphone coordinate") from exc
                require(math.isfinite(value), "Nonfinite microphone coordinate")
                xyz.append(value)
            positions.append(local_vector(rotation,
                tuple(x - p for x, p in zip(xyz, array_pos))))
        require(all(distance(x, positions[0]) <= ARRAY_LIMIT_M
                    for x in positions),
                "LOCATA microphone geometry changes during segment")
        microphones.append({"channel": mic_id - 1,
                            "xyz_m": list(positions[0])})
    reference = local_vector(first_rot,
        tuple(a - b for a, b in zip(first_source_pos, first_pos)))
    norm = math.sqrt(sum(x * x for x in reference))
    require(math.isfinite(norm) and norm > 0.1,
            "LOCATA source coincides with array or has invalid position")
    direction = [x / norm for x in reference]
    return {"microphones": microphones}, direction


def wav_channels(path):
    with wave.open(str(path), "rb") as stream:
        return stream.getnchannels(), stream.getframerate(), stream.getnframes()


def extract_subclip(input_wav, output_wav, start_s, duration_s, channels,
                    ffmpeg="ffmpeg"):
    desired_frames = int(round(duration_s * FS))
    require(desired_frames > 0 and desired_frames % HOP == 0,
            "LOCATA segment length must contain full 128-frame hops")
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "audio.raw"
        # Explicit fixed-rate output, preserving all microphone channels.
        # ffmpeg decoding/resampling is distinct from ground-truth generation.
        cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin",
               "-ss", format(start_s, ".9f"), "-i", str(input_wav),
               "-t", format(duration_s, ".9f"), "-map", "0:a:0",
               "-ac", str(channels), "-ar", str(FS),
               "-c:a", "pcm_s16le", "-f", "s16le", "-y", str(raw)]
        subprocess.run(cmd, check=True, timeout=180)
        target_bytes = desired_frames * channels * 2
        require(raw.stat().st_size >= target_bytes,
                "Resampler returned fewer frames than requested")
        with raw.open("rb") as src, wave.open(str(output_wav), "wb") as dest:
            dest.setnchannels(channels)
            dest.setframerate(FS)
            dest.setsampwidth(2)
            remaining = target_bytes
            while remaining:
                chunk = src.read(min(1024 * 1024, remaining))
                require(bool(chunk), "Unexpected short ffmpeg output")
                dest.writeframesraw(chunk)
                remaining -= len(chunk)


def prepare(directory, array_name, source_name, start_s, end_s, clip_id,
            corpus, ffmpeg="ffmpeg"):
    for value in (array_name, source_name, clip_id):
        require(bool(re.fullmatch(r"[a-zA-Z0-9_-]{1,64}", value)),
                "Invalid LOCATA array/source/recording ID")
    require(all(math.isfinite(t) for t in (start_s, end_s)) and
            0 <= start_s < end_s, "Invalid selected clip interval")
    audio = directory / f"audio_array_{array_name}.wav"
    timeline = directory / f"audio_array_timestamps_{array_name}.txt"
    array_pose = directory / f"position_array_{array_name}.txt"
    source_pose = directory / f"position_source_{source_name}.txt"
    require(all(path.is_file() for path in (audio, timeline, array_pose, source_pose)),
            "Missing original LOCATA WAV/timestamps/OptiTrack pose data")
    channels, fs, frames = wav_channels(audio)
    require(2 <= channels <= 64 and fs > 0, "Invalid LOCATA WAV format")
    require(end_s <= frames / fs, "Requested LOCATA segment exceeds audio")
    first_timestamp = next(locata_rows(timeline), None)
    require(first_timestamp is not None, "Empty LOCATA audio timestamps")
    origin = time_value(first_timestamp)
    start = origin + start_s
    end = origin + end_s
    array_rows = take_static_poses(array_pose, start, end, "array")
    source_rows = take_static_poses(source_pose, start, end, "source")
    geometry, direction = static_reference(array_rows, source_rows)
    require(len(geometry["microphones"]) == channels,
            "LOCATA microphone mapping does not match audio channel count")
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        wav = root / "clip.wav"
        extract_subclip(audio, wav, start_s, end_s - start_s, channels, ffmpeg)
        annotation = root / "reference.json"
        raw_sha256 = hashlib.sha256()
        for path in (audio, timeline, array_pose, source_pose):
            file_hash = hashlib.sha256()
            with path.open("rb") as input_file:
                for block in iter(lambda: input_file.read(1024 * 1024), b""):
                    file_hash.update(block)
            raw_sha256.update(file_hash.digest())
        annotation.write_text(json.dumps({
            "id": clip_id, "intervals": [{
                "source_id": source_name,
                "start_s": 0.0,
                "end_s": int(round((end_s - start_s) * FS)) / FS,
                "direction_xyz": direction,
            }],
            "provenance": {
                "dataset": "LOCATA", "doi": "10.5281/zenodo.3630471",
                "array": array_name, "source": source_name,
                "original_audio_start_s": start_s,
                "original_audio_end_s": end_s,
                "source_files_combined_sha256": raw_sha256.hexdigest(),
                "resampler": "ffmpeg/pcm_s16le/16000",
                "pose_transform": "array_local=R_transpose(world-array_origin)",
                "speech_interval": "independently_selected",
            }
        }), encoding="utf-8")
        import_recording(wav, annotation, corpus, geometry)
    return {"source": source_name, "array": array_name,
            "direction_xyz": direction, "recording_id": clip_id}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--locata-recording", type=Path, required=True,
                        help="Extracted LOCATA task1/task2 recording/ARRAY directory")
    parser.add_argument("--array", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--start-s", type=float, required=True)
    parser.add_argument("--end-s", type=float, required=True)
    parser.add_argument("--id", required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    args = parser.parse_args()
    report = prepare(args.locata_recording, args.array, args.source,
                     args.start_s, args.end_s, args.id, args.corpus,
                     ffmpeg=args.ffmpeg)
    print("locata-import=ok", json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired, StopIteration) as error:
        raise SystemExit("locata-import=fail: " + str(error)) from error
