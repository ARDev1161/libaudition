#!/usr/bin/env python3
"""Acoustix-backed, headless room-acoustics fixture for libaudition.

The scene validator needs only the standard library. Rendering uses the CPU
PyRoomAcousticsRoom backend from Acoustix and preserves all microphone channels
and direct/reverberant energy without per-microphone normalization.

Movement is approximated by overlap-add of emission-time RIR snapshots.
This is an offline test fixture, not a validated Doppler/through-wall solver.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import subprocess
import wave
from pathlib import Path

from acoustix_metrics import compute_metrics


def check_scene(scene: dict) -> dict:
    if scene.get("schema") != "libaudition.acoustix-scene.v1":
        raise ValueError("Unsupported scene schema")
    fs = scene["sample_rate_hz"]
    duration = float(scene["duration_s"])
    dims = scene["room"]["dimensions_m"]
    if fs != 16000 or not 0 < duration <= 300 or len(dims) != 3:
        raise ValueError("Expected 16 kHz, positive duration and 3D room")
    if not 0 < float(scene["room"]["rt60_s"]) <= 2:
        raise ValueError("RT60 must be in (0, 2] seconds")
    if any(not 1 < float(v) < 100 for v in dims):
        raise ValueError("Invalid room dimensions")
    mics = scene["robot"]["microphones_m"]
    if not 2 <= len(mics) <= 32 or any(len(v) != 3 for v in mics):
        raise ValueError("Invalid microphone geometry")
    traj = scene["robot"]["trajectory"]
    if not traj or traj[0]["t_s"] != 0 or traj[-1]["t_s"] < duration:
        raise ValueError("Robot trajectory must cover entire duration")
    names = set()
    for item in scene["sources"]:
        if item["id"] in names or item["kind"] not in {"voice", "fridge", "ac", "car"}:
            raise ValueError("Duplicate source ID or unknown kind")
        names.add(item["id"])
        for start, stop in item["active_s"]:
            if not 0 <= start < stop <= duration:
                raise ValueError("Invalid activity interval for " + item["id"])
        for point in [item["position_m"]] + [p["position_m"] for p in item.get("trajectory", [])]:
            if len(point) != 3 or any(not 0 < v < limit for v, limit in zip(point, dims)):
                raise ValueError("Source outside room: " + item["id"])
        if item.get("trajectory"):
            validate_trajectory(item["trajectory"], duration)
    validate_trajectory(traj, duration)
    for pose in traj:
        x, y, z = pose["position_m"]
        if not (0.15 < x < dims[0] - 0.15 and 0.15 < y < dims[1] - 0.15
                and 0.15 < z < dims[2] - 0.15):
            raise ValueError("Robot outside room or too close to wall")
    return {"sources": len(names), "voice_sources": sum(s["kind"] == "voice" for s in scene["sources"]),
            "microphones": len(mics), "duration_s": duration, "sample_rate_hz": fs}


def validate_trajectory(points: list, duration: float) -> None:
    times = [float(p["t_s"]) for p in points]
    if times[0] != 0 and len(points) == 1:
        raise ValueError("Single pose must start at t=0")
    if any(not math.isfinite(t) for t in times) or any(a >= b for a, b in zip(times, times[1:])):
        raise ValueError("Trajectory times must strictly increase")
    if times[0] > 0 or times[-1] < duration:
        # A moving source may deliberately only move during a subinterval.
        # Its endpoint positions are held before/after the keyframes.
        if len(points) == 1 or times[0] < 0 or times[-1] > duration:
            raise ValueError("Trajectory outside scenario")
    if any(len(p["position_m"]) != 3 for p in points):
        raise ValueError("Expected 3D poses")


def pose_at(points: list, t: float) -> tuple[list[float], float]:
    if t <= points[0]["t_s"]:
        return list(points[0]["position_m"]), float(points[0].get("yaw_deg", 0))
    if t >= points[-1]["t_s"]:
        return list(points[-1]["position_m"]), float(points[-1].get("yaw_deg", 0))
    for a, b in zip(points, points[1:]):
        if a["t_s"] <= t <= b["t_s"]:
            ratio = (t - a["t_s"]) / (b["t_s"] - a["t_s"])
            pos = [a["position_m"][i] * (1 - ratio) + b["position_m"][i] * ratio
                   for i in range(3)]
            yaw_a, yaw_b = float(a.get("yaw_deg", 0)), float(b.get("yaw_deg", 0))
            turn = (yaw_b - yaw_a + 180) % 360 - 180
            return pos, yaw_a + turn * ratio
    raise AssertionError("Trajectory interpolation failed")


def source_position(src: dict, t: float) -> list[float]:
    return pose_at(src["trajectory"], t)[0] if src.get("trajectory") else src["position_m"]


def microphone_world_position(robot: list[float], yaw_deg: float,
                              offset: list[float]) -> list[float]:
    """Rigid robot-frame -> world-frame transform, with +yaw CCW about Z."""
    a = math.radians(yaw_deg)
    cs, sn = math.cos(a), math.sin(a)
    return [robot[0] + cs * offset[0] - sn * offset[1],
            robot[1] + sn * offset[0] + cs * offset[1],
            robot[2] + offset[2]]


def source_body_angles(robot: list[float], yaw_deg: float,
                       source: list[float]) -> tuple[float, float]:
    """Body-frame azimuth and elevation in degrees (robot moves, source fixed)."""
    dx, dy, dz = (source[i] - robot[i] for i in range(3))
    bearing = (math.degrees(math.atan2(dy, dx)) - yaw_deg + 180) % 360 - 180
    elevation = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    return bearing, elevation


def make_source_signal(src: dict, fs: int, frames: int, index: int,
                       rng, speech_dir: Path | None):
    import numpy as np
    from scipy.signal import butter, sosfilt
    t = np.arange(frames, dtype=np.float64) / fs
    kind = src["kind"]
    if kind == "voice" and speech_dir is not None:
        path = speech_dir / (src["id"] + ".wav")
        if not path.is_file():
            raise FileNotFoundError("Missing speech recording: " + str(path))
        with wave.open(str(path)) as wav:
            if wav.getnchannels() != 1 or wav.getsampwidth() != 2 or wav.getframerate() != fs:
                raise ValueError("Speech WAV must be mono PCM16 at 16 kHz: " + str(path))
            x = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2").astype(np.float64) / 32768
        output = np.zeros(frames, dtype=np.float64)
        output[:min(frames, len(x))] = x[:frames]
    elif kind == "voice":
        # Voiced-like excitation is NOT intelligible speech and must not
        # be used as ASR accuracy evidence. Supply --speech-dir for that.
        f0 = 115 + 17 * index
        phase = 2 * math.pi * (f0 * t + 9 * np.sin(2 * math.pi * 0.35 * t))
        output = sum(np.sin(h * phase + h * index * .21) / (h ** 1.2)
                     for h in range(1, 12))
        output *= 0.018 * (0.7 + 0.3 * np.sin(2 * math.pi * (2.4 + index * .2) * t) ** 2)
    elif kind == "fridge":
        output = (.012 * np.sin(2 * math.pi * 50 * t)
                  + .004 * np.sin(2 * math.pi * 100 * t)
                  + .002 * rng.standard_normal(frames))
    elif kind == "ac":
        noise = rng.standard_normal(frames)
        output = .012 * sosfilt(butter(3, 1200, btype="low", fs=fs, output="sos"), noise)
    else:  # A drive-by noise projected onto a position near the window.
        noise = rng.standard_normal(frames)
        output = .025 * sosfilt(butter(3, [90, 2400], btype="band",
                                      fs=fs, output="sos"), noise)
        # Smooth approach and recession; no claim of validated Doppler physics.
        output *= np.exp(-.5 * ((t - 7.1) / .75) ** 2)
    mask = np.zeros(frames, dtype=np.float64)
    ramp = max(1, int(.03 * fs))
    for begin, end in src["active_s"]:
        start, stop = max(0, round(begin * fs)), min(frames, round(end * fs))
        if stop <= start:
            continue
        mask[start:stop] = 1
        span = min(ramp, (stop - start) // 2)
        if span:
            mask[start:start + span] = np.linspace(0, 1, span, endpoint=False)
            mask[stop - span:stop] = np.linspace(1, 0, span, endpoint=False)
    return (output * mask).astype(np.float32)


def rir_snapshot(scene: dict, t: float):
    import numpy as np
    import sys
    from types import ModuleType

    # Acoustix imports soundcard unconditionally in acoustix.utils even when
    # only calculating CPU RIRs. On headless CI soundcard attempts to connect
    # to PulseAudio *at import time* and fails without an audio server.
    # No playback/capture API is used by this fixture: isolate that optional
    # hardware dependency in this dedicated simulation process.
    sys.modules.setdefault("soundcard", ModuleType("soundcard"))
    from acoustix.room import Microphone, Source, PyRoomAcousticsRoom, Room

    # Acoustix 1.0.0 currently reads mic.sampling_frequency, which its own
    # Microphone class does not set. Fix that mismatch in this test adapter,
    # without modifying Acoustix or distributing its GPL source.
    class CpuRoom(PyRoomAcousticsRoom):
        def add_microphone(self, mic, clip_to_grid=False):
            Room.add_microphone(self, mic, clip_to_grid=clip_to_grid)
            self.pa_room.add_microphone(loc=mic.position, fs=int(self.sampling_frequency))

    dx, dy, dz = scene["room"]["dimensions_m"]
    fs = scene["sample_rate_hz"]
    room = CpuRoom(size_x=dx, size_y=dy, height=dz,
                   rt_60=scene["room"]["rt60_s"], sampling_frequency=fs)
    rpos, yaw = pose_at(scene["robot"]["trajectory"], t)
    for i, offset in enumerate(scene["robot"]["microphones_m"]):
        room.add_microphone(Microphone(
            name="mic_" + str(i),
            position=np.asarray(microphone_world_position(rpos, yaw, offset),
                                dtype=float)))
    for s in scene["sources"]:
        room.add_source(Source(name=s["id"], position=np.asarray(source_position(s, t),
                                                              dtype=float)))
    room.pre_compute_rir()
    # Acoustix Room.get_rir() assumes NumPy tuple indexing, but its CPU
    # PyRoomAcousticsRoom backend exposes nested Python lists [mic][source].
    # Access the *actual* backend layout rather than altering upstream.
    rirs = [[np.asarray(room.rir[mi][si], dtype=np.float32)
             for si in range(len(scene["sources"]))]
            for mi in range(len(scene["robot"]["microphones_m"]))]
    if any(len(h) == 0 or not np.all(np.isfinite(h)) for mic in rirs for h in mic):
        raise ValueError("Acoustix returned empty or non-finite RIR")
    return rirs


def write_pcm16(path: Path, samples, fs: int):
    import numpy as np
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = (np.clip(samples, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(pcm.shape[1])
        w.setsampwidth(2)
        w.setframerate(fs)
        w.writeframes(pcm.tobytes())


def render(scene: dict, args) -> None:
    import numpy as np
    from scipy.signal import fftconvolve
    fs = scene["sample_rate_hz"]
    frames = round(min(scene["duration_s"], args.max_seconds or scene["duration_s"]) * fs)
    hop = round(fs * args.hop_ms / 1000)
    if not 0 < hop <= fs:
        raise ValueError("hop-ms must be in (0, 1000]")
    count = len(scene["sources"])
    mics = len(scene["robot"]["microphones_m"])
    rng = np.random.default_rng(args.seed)
    dry = [make_source_signal(s, fs, frames, i, rng, args.speech_dir)
           for i, s in enumerate(scene["sources"])]
    # The existing ODAS offline runner consumes only complete 128-frame
    # hops. Pad the post-reverberation silence, not the generated content.
    output_frames = math.ceil((frames + 3 * fs) / 128) * 128
    separated = np.zeros((count, mics, output_frames), dtype=np.float32)
    max_rir_samples = 0
    snapshots = 0
    # Partition-of-unity triangular emission windows. Input energy belongs
    # to adjacent snapshots; old reflection tails remain in the output.
    # Include a final beyond-end snapshot: otherwise the last emission
    # interval is attenuated because the triangular windows do not sum to 1.
    for center in range(0, frames + hop, hop):
        start = max(0, center - hop + 1)
        stop = min(frames, center + hop)
        indices = np.arange(start, stop)
        weights = (1 - np.abs(indices - center) / hop).astype(np.float32)
        rirs = rir_snapshot(scene, center / fs)
        snapshots += 1
        for si in range(count):
            chunk = dry[si][start:stop] * weights
            if not np.any(chunk):
                continue
            for mi in range(mics):
                h = rirs[mi][si]
                max_rir_samples = max(max_rir_samples, len(h))
                convolution = fftconvolve(chunk, h, mode="full")
                end = start + len(convolution)
                if end > output_frames:
                    raise ValueError("RIR tail exceeds reserved output duration")
                separated[si, mi, start:end] += convolution
    wet = np.sum(separated, axis=0).T
    reference = np.zeros(output_frames, dtype=np.float32)
    reference[:frames] = sum(dry)
    peak = max(float(np.max(np.abs(wet))), float(np.max(np.abs(reference))))
    if peak == 0 or not np.isfinite(peak):
        raise ValueError("Generated silent or non-finite audio")
    scale = min(1.0, 0.94 / peak)
    raw = np.zeros((output_frames, 6), dtype=np.float32)
    raw[:, 0] = reference * scale
    raw[:, 1:5] = wet * scale
    # Channel 5 remains silent playback reference, following ReSpeaker fixture.
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    write_pcm16(out / "input.wav", raw, fs)
    if args.stems:
        from acoustix_signal_contract import audit_audio
        for si, s in enumerate(scene["sources"]):
            write_pcm16(out / "stems" / (s["id"] + ".wav"),
                        separated[si].T * scale, fs)
        signal_audit = audit_audio(out / "input.wav", out / "stems",
                                   [s["id"] for s in scene["sources"]],
                                   sample_rate=fs)
        (out / "signal_audit.json").write_text(
            json.dumps(signal_audit, indent=2) + "\n")
    # Oracle bearings are in the robot BODY frame, not world coordinates.
    gt = []
    for sample in range(0, frames, 128):
        t = (sample + 64) / fs
        robot, yaw = pose_at(scene["robot"]["trajectory"], t)
        active = []
        for s in scene["sources"]:
            if any(start <= t < stop for start, stop in s["active_s"]):
                pos = source_position(s, t)
                bearing, elevation = source_body_angles(robot, yaw, pos)
                active.append({"id": s["id"], "kind": s["kind"],
                               "bearing_deg": round(bearing, 4),
                               "elevation_deg": round(elevation, 4)})
        gt.append({"hop": sample // 128, "t_s": round(t, 4),
                   "robot_position_m": [round(v, 4) for v in robot],
                   "robot_yaw_deg": round(yaw, 4), "sources": active})
    manifest = {"schema": "libaudition.acoustix-result.v1",
                "scene_sha256": hashlib.sha256(args.scene.read_bytes()).hexdigest(),
                "source_count": count, "mic_count": mics, "simulated_frames": frames,
                "wav_frames": output_frames, "sample_rate_hz": fs,
                "rir_snapshots": snapshots, "max_rir_samples": max_rir_samples,
                "normalization": scale,
                "physical_limitations": [
                    "Rectangular room, image-source RIR approximation",
                    "Car projected into room through a virtual window emitter",
                    "No validated Doppler or moving-microphone phase-continuity model",
                    "Synthetic voices are not intelligible speech unless --speech-dir is set"],
                "speech_recordings_supplied": args.speech_dir is not None,
                "ground_truth": gt}
    (out / "ground_truth.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if args.runner:
        folder = out / "odas"
        folder.mkdir(exist_ok=True)
        result = subprocess.run([str(args.runner.resolve()), str(out / "input.wav"),
                                 str(folder)], capture_output=True, text=True, timeout=240)
        (out / "runner.stdout.txt").write_text(result.stdout)
        (out / "runner.stderr.txt").write_text(result.stderr)
        if result.returncode:
            raise RuntimeError("libaudition offline runner failed: " + result.stderr[-1200:])
        summary = json.loads((folder / "summary.json").read_text())
        if summary["input_frames"] != output_frames or summary["recorder_drops"]:
            raise RuntimeError("Audio frame mismatch or offline recorder drop")
        observations = list(csv.DictReader((folder / "observations.csv").open(newline="")))
        # Track persistence is measured from the last nonzero mic PCM sample,
        # not the scene's nominal duration (RIR tails can extend beyond it).
        encoded_mics = (raw[:, 1:5] * 32767).astype(np.int16)
        audible_samples = np.flatnonzero(np.any(encoded_mics != 0, axis=1))
        silence_start = int(audible_samples[-1]) + 1 if len(audible_samples) else 0
        metrics = compute_metrics(gt, observations, summary,
                                  silence_start_frame=silence_start)
        (out / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
        if not metrics["potential_observations"]:
            raise RuntimeError("No ODAS SSL potential source observations")
        if args.compare_capacity:
            # The exact same WAV is replayed with a 2x2 independent sweep
            # of the candidate and tracker capacity limits. No re-render
            # of source signals or RIRs: differences come from ODAS alone.
            def capacity_summary(result_metrics, result_summary, rows):
                by_hop_peak = {}
                for row in rows:
                    if row["kind"] != "proposal":
                        continue
                    hop_id = int(row["hop"])
                    by_hop_peak[hop_id] = max(
                        by_hop_peak.get(hop_id, 0.0), float(row["activity"]))
                peaks = sorted(by_hop_peak.values())
                midpoint = len(peaks) // 2
                peak_median = ((peaks[midpoint - 1] + peaks[midpoint]) / 2
                               if len(peaks) % 2 == 0 and peaks
                               else peaks[midpoint] if peaks else None)
                return {
                    "configured_max_tracks": result_summary["sst_max_tracks"],
                    "configured_ssl_potential_sources":
                        result_summary["ssl_potential_source_count"],
                    "configured_sst_input_potentials":
                        result_summary["sst_input_potential_limit"],
                    "max_simultaneous_tracks":
                        result_metrics["max_simultaneous_tracks"],
                    "matched_voice_source_fraction":
                        result_metrics["matched_voice_source_fraction"],
                    "matched_voice_source_hops":
                        result_metrics["matched_voice_source_hops"],
                    "matched_median_3d_angular_error_deg":
                        result_metrics["matched_median_3d_angular_error_deg"],
                    "matched_p90_3d_angular_error_deg":
                        result_metrics["matched_p90_3d_angular_error_deg"],
                    "matched_median_azimuth_error_deg":
                        result_metrics["matched_median_azimuth_error_deg"],
                    "matched_p90_azimuth_error_deg":
                        result_metrics["matched_p90_azimuth_error_deg"],
                    "adjacent_matched_id_switches":
                        result_metrics["adjacent_matched_id_switches"],
                    "microphone_digital_silence_track_observations":
                        result_metrics["microphone_digital_silence_track_observations"],
                    "potential_observations": result_metrics["potential_observations"],
                    "median_peak_ssl_activity": peak_median,
                    "track_observations": result_metrics["track_observations"],
                    "recordings": result_metrics["recordings"]
                }

            configs = {"4x4": capacity_summary(metrics, summary, observations)}
            raw_results = {"4x4": metrics}
            # Retain the original SSL observations for the isolation contract:
            # changing *only* the SST input message must never change raw SSL.
            all_eight_ssl = None
            capped_eight_ssl = None

            def ssl_observation_signature(rows):
                return [(r["hop"], r["id"], r["x"], r["y"], r["z"], r["activity"])
                        for r in rows if r["kind"] == "proposal"]
            for potential_count, track_count, sst_limit in (
                    (4, 8, 0), (5, 8, 0), (6, 8, 0), (7, 8, 0),
                    (8, 4, 0), (8, 8, 0), (8, 8, 4)):
                name = str(potential_count) + "x" + str(track_count)
                if sst_limit:
                    name += "_sst" + str(sst_limit)
                extra_dir = out / ("odas_" + name)
                extra_dir.mkdir(exist_ok=True)
                result = subprocess.run(
                    [str(args.runner.resolve()), str(out / "input.wav"),
                     str(extra_dir), "--max-tracks", str(track_count),
                     "--potential-sources", str(potential_count),
                     "--sst-input-potentials", str(sst_limit)],
                    capture_output=True, text=True, timeout=240)
                (out / ("runner_" + name + ".stdout.txt")).write_text(result.stdout)
                (out / ("runner_" + name + ".stderr.txt")).write_text(result.stderr)
                if result.returncode:
                    raise RuntimeError("ODAS " + name + " failed: " + result.stderr[-1200:])
                extra_summary = json.loads((extra_dir / "summary.json").read_text())
                if (extra_summary["input_frames"] != output_frames
                        or extra_summary["recorder_drops"]):
                    raise RuntimeError("ODAS " + name + " lost frames or recording data")
                extra_rows = list(csv.DictReader(
                    (extra_dir / "observations.csv").open(newline="")))
                extra_metrics = compute_metrics(
                    gt, extra_rows, extra_summary,
                    silence_start_frame=silence_start)
                (out / ("metrics_" + name + ".json")).write_text(
                    json.dumps(extra_metrics, indent=2) + "\n")
                if not extra_metrics["potential_observations"]:
                    raise RuntimeError("ODAS " + name + " returned no SSL proposals")
                raw_results[name] = extra_metrics
                configs[name] = capacity_summary(
                    extra_metrics, extra_summary, extra_rows)
                if name == "8x8":
                    all_eight_ssl = ssl_observation_signature(extra_rows)
                elif name == "8x8_sst4":
                    capped_eight_ssl = ssl_observation_signature(extra_rows)

            if not all_eight_ssl or all_eight_ssl != capped_eight_ssl:
                raise RuntimeError(
                    "Independent SST input projection modified raw SSL observations")
            # This fixture established a reproducible regression: full
            # synthetic 12 s with SSL=8 had zero tracks. The opt-in bounded
            # input must preserve nonzero tracking, not just report PASS
            # because a Python/C++ process exited successfully.
            if (frames == 12 * fs and args.speech_dir is None
                    and configs["8x8_sst4"]["track_observations"] == 0):
                raise RuntimeError(
                    "Bounded SST input failed to restore any source tracks")
            comparison = {
                "schema": "libaudition.acoustix-capacity-comparison.v4",
                "method": "identical 4-mic WAV; SSL 4-8 sweep and independent 8->4 SST candidate projection",
                "capacity_axes": {
                    "first": "SSL potential source count",
                    "second": "SST maximum track count"
                },
                "configurations": configs,
                "baseline": configs["4x4"],
                "expanded": configs["8x8"],
                "expanded_sst_input_bounded": configs["8x8_sst4"],
                "raw_ssl_eight_candidate_output_identical_with_and_without_sst_cap": True,
                "deltas_vs_4x4": {
                    name: {
                        "matched_voice_source_hops":
                            values["matched_voice_source_hops"] -
                            metrics["matched_voice_source_hops"],
                        "max_simultaneous_tracks":
                            values["max_simultaneous_tracks"] -
                            metrics["max_simultaneous_tracks"]
                    }
                    for name, values in raw_results.items()
                    if name != "4x4"
                },
                "interpretation_limitations": [
                    "Capacity changes may change internal ODAS hypothesis distribution",
                    "SST input projection is experimental and preserves raw eight-slot SSL output",
                    "Tracking threshold tuning is unchanged in all runs",
                    "Four microphones may not resolve seven simultaneous sources",
                    "Synthetic voiced-like signals cannot validate real-speech ASR or leakage"
                ]
            }
            (out / "capacity_comparison.json").write_text(
                json.dumps(comparison, indent=2) + "\n")
    print(json.dumps({"output": str(out), "rir_snapshots": snapshots,
                      "pcm_channels": 6, "frames": output_frames}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--max-seconds", type=float, help="Render only the first N seconds")
    parser.add_argument("--hop-ms", type=int, default=250)
    parser.add_argument("--seed", type=int, default=1161)
    parser.add_argument("--speech-dir", type=Path, help="speaker_1.wav .. speaker_7.wav, mono PCM16/16k")
    parser.add_argument("--stems", action="store_true", help="Export per-source 4-channel WAVs")
    parser.add_argument("--runner", type=Path, help="Built audition_offline_integration executable")
    parser.add_argument("--compare-capacity", action="store_true",
                        help="Replay identical WAV through both default 4x4 and expanded 8x8 ODAS")
    args = parser.parse_args()
    scene = json.loads(args.scene.read_text(encoding="utf-8"))
    stats = check_scene(scene)
    if args.validate_only:
        print(json.dumps(stats, sort_keys=True))
        return
    if not args.output:
        parser.error("--output is required when rendering")
    if args.max_seconds is not None and not 0 < args.max_seconds <= scene["duration_s"]:
        parser.error("--max-seconds must be within scene duration")
    if args.compare_capacity and not args.runner:
        parser.error("--compare-capacity requires --runner")
    render(scene, args)


if __name__ == "__main__":
    main()
