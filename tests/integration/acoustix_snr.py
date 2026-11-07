#!/usr/bin/env python3
"""Reverberant microphone-domain target speech vs environmental-noise SNR.

SNR is calculated at microphone 0 *after* the real Acoustix RIR renderer,
not on pre-propagation dry signals. Voice interferers are deliberately NOT
part of background noise: this gate must never masquerade as multi-speaker
separation SIR/SI-SDR. All background stems receive a single common gain;
the original acoustic transfer functions and relative noise composition
remain unchanged.
"""
from __future__ import annotations

import math


def background_snr_gain(voice: list[float], background: list[float],
                        active_mask: list[bool],
                        requested_snr_db: float) -> tuple[float, dict]:
    if (not isinstance(requested_snr_db, (float, int))
            or isinstance(requested_snr_db, bool)
            or not math.isfinite(requested_snr_db)
            or not -30.0 <= requested_snr_db <= 60.0):
        raise ValueError("Requested background SNR must be finite in [-30, 60] dB")
    if not voice or len(voice) != len(background) or len(voice) != len(active_mask):
        raise ValueError("SNR vectors/mask must have equal nonzero lengths")
    if any(type(on) is not bool for on in active_mask):
        raise ValueError("SNR activity mask must contain only booleans")
    indices = [i for i, on in enumerate(active_mask) if on]
    if not indices:
        raise ValueError("SNR needs at least one active speech sample")
    if any(not math.isfinite(float(voice[i])) or not math.isfinite(float(background[i]))
           for i in range(len(voice))):
        raise ValueError("SNR streams must be finite")
    # Measure the coherent sums of all speech and all environmental sources
    # on the SAME microphone and time segment. Not an average of individual
    # source dB levels or a free-field geometric attenuation estimate.
    voice_power = math.fsum(float(voice[i]) ** 2 for i in indices) / len(indices)
    noise_power = math.fsum(float(background[i]) ** 2 for i in indices) / len(indices)
    if voice_power <= 0 or noise_power <= 0:
        raise ValueError("Target speech and background must be non-silent")
    gain = math.sqrt(voice_power / (noise_power * 10.0 ** (requested_snr_db / 10.0)))
    if not math.isfinite(gain) or gain <= 0:
        raise ValueError("Invalid inferred background gain")
    measured = 10.0 * math.log10(voice_power / (noise_power * gain * gain))
    if abs(measured - requested_snr_db) > 1e-9:
        raise ValueError("SNR target is numerically inconsistent")
    return gain, {
        "requested_voice_to_background_snr_db": float(requested_snr_db),
        "measured_voice_to_background_snr_db": measured,
        "background_gain": gain,
        "measured_microphone_index": 0,
        "active_sample_count": len(indices),
        "voice_power_before_global_pcm_normalization": voice_power,
        "background_power_before_gain": noise_power,
        "definition": "post-RIR coherent speech sum / post-RIR environmental noise sum, mic 0, emission-active speech windows",
        "excludes": ["other-voice interference from background", "SIR", "SI-SDR", "ASR WER"],
    }


def apply_background_snr(separated, sources: list[dict],
                         fs: int, frames: int, requested_snr_db: float):
    """Adjust environmental stems in-place on rendered (source,mic,time) audio."""
    import numpy as np
    if (fs <= 0 or frames <= 0 or separated.ndim != 3
            or separated.shape[0] != len(sources)
            or separated.shape[1] < 1 or separated.shape[2] < frames):
        raise ValueError("Invalid separated audio shape or clock")
    voices = [i for i, src in enumerate(sources) if src["kind"] == "voice"]
    environment = [i for i, src in enumerate(sources) if src["kind"] != "voice"]
    if not voices or not environment:
        raise ValueError("SNR requires both voice and environmental noise sources")
    mask = np.zeros(frames, dtype=np.bool_)
    for i in voices:
        for begin, end in sources[i]["active_s"]:
            first = max(0, round(begin * fs))
            last = min(frames, round(end * fs))
            if first < last:
                mask[first:last] = True
    voice_mix = np.sum(separated[voices, 0, :frames], axis=0, dtype=np.float64)
    background_mix = np.sum(separated[environment, 0, :frames], axis=0, dtype=np.float64)
    gain, evidence = background_snr_gain(
        voice_mix.tolist(), background_mix.tolist(), mask.tolist(),
        requested_snr_db)
    separated[environment] *= gain
    if not np.all(np.isfinite(separated)):
        raise ValueError("SNR adjustment created nonfinite audio")
    evidence["voice_source_ids"] = [sources[i]["id"] for i in voices]
    evidence["environment_source_ids"] = [sources[i]["id"] for i in environment]
    evidence["measurement_reference"] = "Acoustix wet stems, before shared PCM gain"
    return evidence
