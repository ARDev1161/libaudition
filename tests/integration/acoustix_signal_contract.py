#!/usr/bin/env python3
"""Validate Acoustix source-stem summation against the exact ODAS input WAV.

Only Python stdlib is required; checks are independent from ODAS results.
Quantization is allowed at most one LSB per stem (+2 margin), not an
arbitrary signal-quality cutoff.
"""
from __future__ import annotations

from array import array
from contextlib import ExitStack
import math
from pathlib import Path
import sys
import wave


def _pcm16(buf: bytes) -> array:
    result = array("h")
    result.frombytes(buf)
    if sys.byteorder != "little":
        result.byteswap()
    return result


def audit_audio(input_wav: Path, stem_dir: Path,
                source_ids: list[str], sample_rate: int = 16000,
                chunk_frames: int = 4096) -> dict:
    if not source_ids or len(source_ids) != len(set(source_ids)):
        raise ValueError("Source IDs must be unique and nonempty")
    if chunk_frames <= 0:
        raise ValueError("chunk_frames must be positive")
    with ExitStack() as stack:
        master = stack.enter_context(wave.open(str(input_wav), "rb"))
        stems = [stack.enter_context(wave.open(str(stem_dir / (sid + ".wav")), "rb"))
                 for sid in source_ids]
        if (master.getnchannels(), master.getframerate(), master.getsampwidth()) != (6, sample_rate, 2):
            raise ValueError("ODAS input must be six-channel PCM16 at configured sample rate")
        total = master.getnframes()
        for name, item in zip(source_ids, stems):
            if (item.getnchannels(), item.getframerate(), item.getsampwidth(),
                    item.getnframes()) != (4, sample_rate, 2, total):
                raise ValueError("Invalid four-channel stem geometry or length: " + name)

        nsrc = len(stems)
        energy = [0] * nsrc
        mic_energy = [0] * 4
        difference_energy = [0] * 3
        max_residual = 0
        peak = 0
        last_nonzero_frame = -1
        frame_pos = 0
        while frame_pos < total:
            take = min(chunk_frames, total - frame_pos)
            mixed = _pcm16(master.readframes(take))
            separated = [_pcm16(item.readframes(take)) for item in stems]
            if len(mixed) != take * 6 or any(len(item) != take * 4 for item in separated):
                raise ValueError("Truncated microphone or source-stem WAV")
            for i in range(take):
                frame = frame_pos + i
                for mi in range(4):
                    value = mixed[i * 6 + 1 + mi]
                    mic_energy[mi] += value * value
                    peak = max(peak, abs(value))
                    if value:
                        last_nonzero_frame = frame
                    predicted = 0
                    for si, stream in enumerate(separated):
                        contribution = stream[i * 4 + mi]
                        energy[si] += contribution * contribution
                        predicted += contribution
                    max_residual = max(max_residual, abs(value - predicted))
                    if mi:
                        diff = value - mixed[i * 6 + mi]
                        difference_energy[mi - 1] += diff * diff
            frame_pos += take

    if max_residual > nsrc + 2:
        raise ValueError(
            f"Mixture and source stems disagree: max {max_residual} LSB, "
            f"allowed {nsrc + 2} LSB from PCM quantization")
    if not all(mic_energy) or not any(difference_energy):
        raise ValueError("Microphones silent or all spatial channels identical")
    if peak >= 32767:
        raise ValueError("Mixed microphone PCM16 contains clipped samples")
    if not any(energy):
        raise ValueError("Every input source stem is silent")
    return {
        "schema": "libaudition.acoustix-signal-audit.v1",
        "sample_rate_hz": sample_rate,
        "input_frames": total,
        "source_count": nsrc,
        "reconstruction_max_residual_lsb": max_residual,
        "quantization_allowance_lsb": nsrc + 2,
        "microphone_peak_pcm16": peak,
        "microphone_rms_pcm16": [round(math.sqrt(x / total), 3) for x in mic_energy],
        "adjacent_microphone_difference_rms_pcm16":
            [round(math.sqrt(x / total), 3) for x in difference_energy],
        "source_rms_pcm16": {
            sid: round(math.sqrt(x / (total * 4)), 3)
            for sid, x in zip(source_ids, energy)
        },
        "post_acoustic_signal_silence_frames": total - last_nonzero_frame - 1,
    }
