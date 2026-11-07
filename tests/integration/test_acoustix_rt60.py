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

from acoustix_reverb import calibrate_scene_rt60, estimate_rt60_t20

REPORT = {}



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
            # Do NOT enlarge the acceptance tolerance to hide a mismatch.
            # The pinned backend can overshoot the Sabine request by >25%.
            raw_rir = rir_snapshot(scene, 0)[0][0]
            raw_metrics = estimate_rt60_t20(raw_rir, fs)
            calibrated_scene, calibration = calibrate_scene_rt60(scene)
            rir = rir_snapshot(calibrated_scene, 0)[0][0]
            metrics = estimate_rt60_t20(rir, fs)
            deviation = (metrics["t60_s"] - requested) / requested
            self.assertLessEqual(abs(deviation), 0.25,
                                 f"Requested RT60={requested}, observed={metrics}")
            self.assertLessEqual(abs(deviation),
                                 abs((raw_metrics["t60_s"] - requested) / requested) + .001)
            self.assertLessEqual(abs(calibration["calibrated_relative_error"]), 0.05)
            self.assertGreater(metrics["r_squared"], 0.85)
            results.append({"requested_rt60_s": requested,
                            "raw_uncalibrated_t60_s": raw_metrics["t60_s"],
                            "effective_backend_rt60_s":
                                calibration["effective_backend_rt60_s"],
                            "calibration_observations": calibration["observations"],
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
