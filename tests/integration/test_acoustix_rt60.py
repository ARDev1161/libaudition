#!/usr/bin/env python3
"""Acoustix S1 reverberation-decay contract.

Schroeder backward-integration T20: calculate the -5 to -25 dB
least-squares energy-decay slope and extrapolate to 60 dB.
The estimator is pure stdlib; --cpu-rir evaluates real pinned Acoustix RIRs.
"""
from __future__ import annotations

import argparse
import json
import math
import unittest
from pathlib import Path

REPORT = {}


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


class T20MetricTests(unittest.TestCase):
    def test_exponential_reference_and_gain_invariance(self):
        fs = 8000
        for expected in (0.20, 0.36, 0.70):
            # Since cumulative energy falls as exp(-13.8155*t/T60),
            # the amplitude envelope is exp(-6.9078*t/T60).
            signal = [math.exp(-math.log(1000.0) * i / (fs * expected))
                      for i in range(4 * fs)]
            measured = estimate_rt60_t20(signal, fs)
            self.assertAlmostEqual(measured["t60_s"], expected,
                                   delta=expected * 0.005)
            scaled = estimate_rt60_t20([v * 0.3 for v in signal], fs)
            self.assertAlmostEqual(measured["t60_s"], scaled["t60_s"],
                                   places=8)
            self.assertGreater(measured["r_squared"], 0.999)

    def test_invalid_impulse_input_fails(self):
        for invalid in ([0.0] * 256, [math.nan] * 256, [math.inf] * 256):
            with self.assertRaises(ValueError):
                estimate_rt60_t20(invalid, 8000)
        with self.assertRaises(ValueError):
            estimate_rt60_t20([1.0] * 10, 8000)
        with self.assertRaises(ValueError):
            estimate_rt60_t20([1.0] * 200, 0)


class ActualCpuRirT60Tests(unittest.TestCase):
    def test_two_target_rt60s_and_monotonic_decay(self):
        from acoustix_scene import rir_snapshot

        fs = 16000
        results = []
        for requested in (0.20, 0.36):
            scene = {
                "sample_rate_hz": fs,
                "room": {"dimensions_m": [7., 7., 2.8],
                         "rt60_s": requested},
                "robot": {
                    "trajectory": [{"t_s": 0, "position_m": [3.5, 3., 1.4],
                                    "yaw_deg": 0}],
                    "microphones_m": [[0., 0., 0.], [0.03, 0., 0.]],
                },
                "sources": [{"id": "impulse", "position_m": [2.5, 3., 1.4]}],
            }
            rir = rir_snapshot(scene, 0)[0][0]
            metrics = estimate_rt60_t20(rir, fs)
            # Frequency-independent diffuse Sabine target has limited
            # agreement with discrete finite-order image-source physics.
            # A strict 25% target prevents accepting a silent/incorrect RIR.
            deviation = (metrics["t60_s"] - requested) / requested
            self.assertLessEqual(abs(deviation), 0.25,
                                 f"Requested RT60={requested}, observed={metrics}")
            self.assertGreater(metrics["r_squared"], 0.85)
            results.append({"requested_rt60_s": requested,
                            "relative_error": deviation,
                            "rir_length_samples": len(rir),
                            **metrics})
        self.assertGreater(results[1]["t60_s"], results[0]["t60_s"] * 1.30)
        REPORT["rt60"] = results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu-rir", action="store_true",
                        help="Test pinned real CPU image-source room responses")
    parser.add_argument("--report", type=Path,
                        help="Publish measured T20/RT60 evidence JSON")
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(T20MetricTests)
    if args.cpu_rir:
        suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(
            ActualCpuRirT60Tests))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({
            "schema": "libaudition.acoustix-reverb-check.v1",
            "passed": True,
            "t20_estimator_reference_passed": True,
            "real_acoustix_cpu_rirs_exercised": args.cpu_rir,
            "evidence": REPORT,
            "model_limitations": [
                "frequency-independent material absorption",
                "stationary shoebox image-source only",
                "finite-order reflection approximation",
                "no claim of real-room calibrated RT60",
            ],
        }, indent=2) + "\n")


if __name__ == "__main__":
    main()
