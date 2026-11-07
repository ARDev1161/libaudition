"""RT60 measurement and one-shot room calibration for Acoustix fixtures.

Uses a finite-order image-source backend and Schroeder T20 decay. A Sabine
parameter is only a target: infer the effective input for the pinned backend
instead of silently calling an unverified nominal RT60 physically exact.
No backend work on the audio real-time path.
"""
from __future__ import annotations

import copy
import math

def estimate_rt60_t20(samples, sample_rate_hz: int) -> dict:
    """Estimate T60 from T20 decay; raise on invalid/nondecaying input."""
    if sample_rate_hz <= 0:
        raise ValueError("Sample rate must be positive")
    values = [float(v) for v in samples]
    if len(values) < 128 or any(not math.isfinite(v) for v in values):
        raise ValueError("Expected finite impulse response with >=128 samples")
    energy = [0.0] * len(values)
    cumulative = 0.0
    for i in range(len(values) - 1, -1, -1):
        cumulative += values[i] * values[i]
        energy[i] = cumulative
    if not math.isfinite(cumulative) or cumulative <= 0:
        raise ValueError("Impulse response has no energy")
    decay_db = [10.0 * math.log10(max(e / cumulative, 1e-300))
                for e in energy]
    sample_indices = [i for i, db in enumerate(decay_db)
                      if -25.0 <= db <= -5.0]
    if len(sample_indices) < 64:
        raise ValueError("Not enough -5..-25 dB energy decay samples")
    n = len(sample_indices)
    mean_x = sum(i for i in sample_indices) / n
    mean_y = sum(decay_db[i] for i in sample_indices) / n
    covariance = sum((i - mean_x) * (decay_db[i] - mean_y)
                     for i in sample_indices)
    variance = sum((i - mean_x) ** 2 for i in sample_indices)
    if variance <= 0 or covariance >= 0:
        raise ValueError("Impulse response does not decay")
    slope_db_per_sample = covariance / variance
    fitted_t60_s = -60.0 / (slope_db_per_sample * sample_rate_hz)
    if not math.isfinite(fitted_t60_s) or fitted_t60_s <= 0:
        raise ValueError("Invalid derived T60")
    fitted_offset = mean_y - slope_db_per_sample * mean_x
    residuals = sum((decay_db[i] - fitted_offset
                     - slope_db_per_sample * i) ** 2 for i in sample_indices)
    variation = sum((decay_db[i] - mean_y) ** 2 for i in sample_indices)
    r_squared = 1.0 - residuals / variation if variation > 0 else 1.0
    return {
        "t60_s": fitted_t60_s,
        "fit_samples": n,
        "fit_window_db": [-5.0, -25.0],
        "r_squared": r_squared,
        "method": "Schroeder energy backward integration, T20 extrapolation",
    }



def calibrate_scene_rt60(scene: dict, *, probe_time_s: float = 0.0,
                         max_iterations: int = 8,
                         relative_tolerance: float = 0.05) -> tuple[dict, dict]:
    """Return a new scene and evidence for a single initial RT60 calibration.

    RIR measurement is from a representative fixed source/mic pair, never
    from audio labels/ODAS outputs. All later moving RIR snapshots use the
    same calibrated effective backend RT60, with no per-snapshot fitting.
    """
    from acoustix_scene import rir_snapshot

    target = float(scene["room"]["rt60_s"])
    if not math.isfinite(target) or target <= 0:
        raise ValueError("RT60 target must be positive and finite")
    if not scene.get("sources") or len(scene["robot"]["microphones_m"]) < 1:
        raise ValueError("Calibration needs a source and microphone")

    # Probe one deterministic representative source and microphone to avoid
    # re-rendering the full 7-source moving scene for each iteration.
    probe = copy.deepcopy(scene)
    probe["sources"] = [probe["sources"][0]]
    probe["robot"]["microphones_m"] = [probe["robot"]["microphones_m"][0]]
    sample_rate = int(scene["sample_rate_hz"])
    observations = []

    def measure(effective_s: float) -> float:
        probe["room"]["rt60_s"] = float(effective_s)
        rir = rir_snapshot(probe, probe_time_s)[0][0]
        metrics = estimate_rt60_t20(rir, sample_rate)
        observed = float(metrics["t60_s"])
        observations.append({"input_rt60_s": float(effective_s),
                             "measured_t20_rt60_s": observed,
                             "fit_r_squared": metrics["r_squared"]})
        # Extreme preliminary bracketing samples may not have a clean
        # Schroeder slope; they can guide interval selection but may never
        # become the accepted final calibrated result.
        return observed

    original = measure(target)
    if observations[-1]["fit_r_squared"] < 0.85:
        raise ValueError("Requested RT60 lacks a credible raw T20 slope")
    best = (abs(original - target), target, original)
    if best[0] > target * relative_tolerance:
        low = target * 0.75
        high = target * 1.35
        measured_low = measure(low)
        measured_high = measure(high)
        if not measured_low < target < measured_high:
            raise ValueError(
                "Cannot bracket RT60 calibration; observed at endpoints: "
                + str((measured_low, measured_high)))
        for _ in range(max_iterations):
            candidate = (low + high) / 2.0
            observed = measure(candidate)
            if (observations[-1]["fit_r_squared"] >= 0.85 and
                    abs(observed - target) < best[0]):
                best = (abs(observed - target), candidate, observed)
            if best[0] <= target * relative_tolerance:
                break
            if observed < target:
                low = candidate
            else:
                high = candidate

    if best[0] > target * relative_tolerance:
        raise ValueError("Failed to calibrate RT60 with credible T20 slope: "
                         + str(observations))

    rendered_scene = copy.deepcopy(scene)
    rendered_scene["room"]["rt60_s"] = best[1]
    evidence = {
        "requested_rt60_s": target,
        "raw_measured_rt60_s": original,
        "effective_backend_rt60_s": best[1],
        "calibrated_probe_rt60_s": best[2],
        "calibrated_relative_error": (best[2] - target) / target,
        "probe_time_s": probe_time_s,
        "probe_source_id": probe["sources"][0]["id"],
        "probe_microphone_index": 0,
        "observations": observations,
        "limitations": [
            "calibration is one stationary source/receiver reference, not per-mic",
            "frequency-independent shoebox image-source model",
            "no validated moving-room phase or physical-array calibration",
        ],
    }
    return rendered_scene, evidence
