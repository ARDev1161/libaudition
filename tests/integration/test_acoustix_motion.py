#!/usr/bin/env python3
"""S1 moving-RIR overlap-add signal-continuity regression.

This test checks for numerical gaps and hard switches at RIR update boundaries,
and convergence under finer time sampling. It is NOT a Doppler accuracy model
or a validation of physical microphone response.
"""
from __future__ import annotations

import argparse
import json
import math
import unittest
from pathlib import Path

from acoustix_scene import render_windowed_rirs

REPORT = {}


def root_mean_square(values) -> float:
    import numpy as np
    return float(np.sqrt(np.mean(np.asarray(values, dtype=np.float64) ** 2)))


class MovingRirContractTests(unittest.TestCase):
    def test_stationary_overlap_add_equals_single_reference_convolution(self):
        import numpy as np
        from scipy.signal import fftconvolve

        fs, frames, hop, total = 16000, 4096, 512, 4608
        scene = {
            "sample_rate_hz": fs,
            "robot": {"microphones_m": [[0, 0, 0], [0.032, 0, 0]]},
            "sources": [{"id": "test", "position_m": [3, 4, 1.5]}],
        }
        dry = np.random.default_rng(1161).standard_normal(frames).astype(np.float32) * 0.01
        rir = np.asarray([0.0, 0.7, -0.2, 0.07, 0.0, -0.03],
                         dtype=np.float32)
        def constant_rir(_scene, _t):
            return [[rir], [rir * np.float32(0.6)]]

        separated, _, snapshots = render_windowed_rirs(
            scene, [dry], frames, hop, total, rir_provider=constant_rir)
        expected = np.zeros((2, total), dtype=np.float32)
        for mi, gain in enumerate((1.0, 0.6)):
            direct = fftconvolve(dry, rir * np.float32(gain), mode="full")
            expected[mi, :len(direct)] = direct

        error = float(np.max(np.abs(separated[0] - expected)))
        self.assertLess(error, 3e-6, f"Static snapshots changed the reference convolution by {error}")
        self.assertGreater(snapshots, 2)
        self.assertGreater(root_mean_square(separated[0]), 0.0)
        REPORT["stationary_partition"] = {
            "source": "deterministic synthetic FIR, independent direct convolution",
            "max_sample_error": error,
            "snapshot_count": snapshots,
        }

    def test_real_cpu_moving_microphone_has_no_rir_update_clicks(self):
        import numpy as np

        fs = 16000
        duration = 1.0
        frames = int(fs * duration)
        total = frames + 2 * fs
        hop = int(0.1 * fs)
        timebase = np.arange(frames) / fs
        # Carrier has energy through every RIR snapshot boundary.
        dry = 0.014 * np.sin(2 * math.pi * 500.0 * timebase)
        ramp = int(0.04 * fs)
        dry[:ramp] *= np.linspace(0, 1, ramp, endpoint=False)
        dry[-ramp:] *= np.linspace(1, 0, ramp, endpoint=False)
        dry = dry.astype(np.float32)

        def scene_with_path(moving: bool):
            end_pos = [4.08, 4.03, 1.5] if moving else [4.0, 4.0, 1.5]
            end_yaw = 12 if moving else 0
            return {
                "sample_rate_hz": fs,
                "room": {"dimensions_m": [7., 7., 2.8], "rt60_s": 0.20},
                "robot": {
                    "trajectory": [
                        {"t_s": 0., "position_m": [4.0, 4.0, 1.5],
                         "yaw_deg": 0},
                        {"t_s": duration, "position_m": end_pos,
                         "yaw_deg": end_yaw},
                    ],
                    "microphones_m": [[0, 0, 0], [0.032, 0, 0]],
                },
                "sources": [{"id": "tone", "position_m": [3., 4., 1.5]}],
            }

        static_scene = scene_with_path(False)
        moving_scene = scene_with_path(True)
        baseline, _, _ = render_windowed_rirs(
            static_scene, [dry], frames, hop, total)
        moving, _, _ = render_windowed_rirs(
            moving_scene, [dry], frames, hop, total)
        finer, _, _ = render_windowed_rirs(
            moving_scene, [dry], frames, hop // 2, total)

        self.assertTrue(np.all(np.isfinite(moving)))
        self.assertTrue(np.all(np.isfinite(finer)))
        stationary_vs_moving = root_mean_square(moving - baseline)
        self.assertGreater(stationary_vs_moving, 1e-5,
                           "Moving geometry had no observable effect")

        # A 12-degree yaw and 8cm translation at 500Hz are deliberately
        # slow relative to the acoustic wavelength; the two snapshot
        # densities should not create substantially different audio.
        start, end = int(.16 * fs), int(.86 * fs)
        ref_energy = root_mean_square(finer[:, :, start:end])
        self.assertGreater(ref_energy, 1e-6)
        relative_hop_error = root_mean_square(
            moving[:, :, start:end] - finer[:, :, start:end]) / ref_energy
        self.assertLessEqual(relative_hop_error, 0.35,
                             f"RIR hop refinement mismatch: {relative_hop_error}")

        max_jump_ratio = 0.0
        min_boundary_envelope_ratio = float("inf")
        for mi in range(2):
            wet = moving[0, mi]
            steady = wet[start:end]
            derivative_reference = float(
                np.quantile(np.abs(np.diff(steady)), 0.99))
            self.assertGreater(derivative_reference, 1e-8)
            for emission_boundary in range(hop, frames - hop, hop):
                # The RIR is applied to emission-time windows. Evaluate
                # the expected acoustic arrival window (≈40+range/c).
                boundary = emission_boundary + 90
                window = wet[boundary - 16: boundary + 16]
                local_jump = float(np.max(np.abs(np.diff(window))))
                max_jump_ratio = max(max_jump_ratio,
                                     local_jump / derivative_reference)
                local_rms = root_mean_square(wet[boundary - 128:boundary + 128])
                surrounding_rms = root_mean_square(wet[boundary - 640:boundary + 640])
                self.assertGreater(surrounding_rms, 1e-7)
                ratio = local_rms / surrounding_rms
                min_boundary_envelope_ratio = min(min_boundary_envelope_ratio, ratio)
                self.assertGreater(ratio, 0.30,
                                   f"Audible dropout at RIR boundary sample {boundary}")
        self.assertLessEqual(max_jump_ratio, 2.0,
                             f"Discontinuous click at RIR update: {max_jump_ratio}")
        REPORT["moving_cpu_rir"] = {
            "geometry": "8cm translation + 12deg yaw; 2 mics; 500Hz tone; 1s",
            "max_boundary_diff_over_global_p99": max_jump_ratio,
            "min_boundary_rms_over_surrounding_rms": min_boundary_envelope_ratio,
            "relative_hop_100ms_vs_50ms_rms_error": relative_hop_error,
            "stationary_to_moving_rms_difference": stationary_vs_moving,
            "qualifications": [
                "not a comparison to continuous physical Doppler",
                "does not prove high-speed robot motion phase correctness",
                "finite-order, stationary-snapshot image-source backend",
            ],
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu-rir", action="store_true",
                        help="Include real pinned CPU moving-RIR test")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(
        MovingRirContractTests)
    if not args.cpu_rir:
        # Unit-level FFT/reference requires NumPy/SciPy, so this test is
        # intentionally part of the optional dedicated Acoustix runner.
        suite = unittest.TestSuite()
        suite.addTest(MovingRirContractTests(
            "test_stationary_overlap_add_equals_single_reference_convolution"))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({
            "schema": "libaudition.acoustix-motion-check.v1",
            "passed": True,
            "cpu_rir_exercised": args.cpu_rir,
            "evidence": REPORT,
        }, indent=2) + "\n")


if __name__ == "__main__":
    main()
