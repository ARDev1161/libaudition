#!/usr/bin/env python3
"""S1 Acoustix geometry/physics regression, independent of ODAS and ASR.

Default mode uses the standard library only. --cpu-rir additionally calls the
real pinned Acoustix PyRoomAcousticsRoom RIR backend; no mock RIR is accepted.
"""
from __future__ import annotations

import argparse
import json
import math
import unittest
from pathlib import Path

from acoustix_scene import (
    direct_path_delay_samples,
    microphone_world_position,
    pose_at,
    source_body_angles,
)

REPORT = {}


class GeometryContractTests(unittest.TestCase):
    def assert_position(self, actual, expected):
        for observed, reference in zip(actual, expected):
            self.assertAlmostEqual(observed, reference, places=10)

    def test_known_mic_offsets_under_robot_yaw(self):
        origin = [4.0, 4.0, 1.5]
        offset = [1.0, 0.25, -0.2]
        self.assert_position(microphone_world_position(origin, 0, offset),
                             [5.0, 4.25, 1.3])
        self.assert_position(microphone_world_position(origin, 90, offset),
                             [3.75, 5.0, 1.3])
        self.assert_position(microphone_world_position(origin, 180, offset),
                             [3.0, 3.75, 1.3])
        self.assert_position(microphone_world_position(origin, -90, offset),
                             [4.25, 3.0, 1.3])

    def test_azimuth_wrap_and_elevation(self):
        origin = [4.0, 4.0, 1.5]
        for world, yaw, expected in (
                ([5, 4, 1.5], 0, 0),
                ([4, 5, 1.5], 0, 90),
                ([4, 3, 1.5], 0, -90),
                ([3, 4, 1.5], 0, -180),
                ([4, 5, 1.5], 90, 0),
                ([5, 4, 1.5], 90, -90),
                ([4, 5, 1.5], -170, -100),
                ([3, 4, 1.5], 90, 90)):
            bearing, elevation = source_body_angles(origin, yaw, world)
            self.assertAlmostEqual(bearing, expected, places=9)
            self.assertAlmostEqual(elevation, 0, places=9)
        az, elev = source_body_angles(origin, 0, [5, 4, 2.5])
        self.assertAlmostEqual(az, 0)
        self.assertAlmostEqual(elev, 45)

    def test_shortest_interpolated_yaw_and_translation(self):
        points = [
            {"t_s": 0, "position_m": [3, 4, 1], "yaw_deg": 170},
            {"t_s": 2, "position_m": [5, 4, 1], "yaw_deg": -170},
        ]
        pose, yaw = pose_at(points, 1.0)
        self.assert_position(pose, [4, 4, 1])
        # Shortest angular arc is +20 degrees through 180 degrees.
        self.assertAlmostEqual((yaw + 180) % 360 - 180, -180)
        self.assert_position(pose_at(points, -1)[0], [3, 4, 1])
        self.assert_position(pose_at(points, 3)[0], [5, 4, 1])

    def test_body_oracle_matches_independent_rotation(self):
        origin = [3.2, 4.1, 1.5]
        for yaw in (-170, -90, -30, 0, 30, 90, 170):
            for az in (-150, -60, -5, 0, 15, 65, 150):
                a = math.radians(yaw + az)
                source = [origin[0] + 1.3 * math.cos(a),
                          origin[1] + 1.3 * math.sin(a), origin[2]]
                bearing, elevation = source_body_angles(origin, yaw, source)
                self.assertAlmostEqual(bearing, az, places=8)
                self.assertAlmostEqual(elevation, 0, places=8)

    def test_absolute_delay_oracle_includes_filter_group_delay(self):
        prediction = direct_path_delay_samples(
            [4, 4, 1.5], 0, [3, 4, 1.5],
            [[0, 0, 0], [1, 0, 0]], 16000, 343.0, 40)
        self.assertAlmostEqual(prediction[0], 40 + 16000 / 343)
        self.assertAlmostEqual(prediction[1], 40 + 32000 / 343)
        with self.assertRaises(ValueError):
            direct_path_delay_samples([4, 4, 1.5], 0, [3, 4, 1.5],
                                      [[0, 0, 0]], 16000, 0, 40)

    def test_rir_emission_windows_form_partition_of_unity(self):
        # Mirrors the window construction used in acoustix_scene.render,
        # including its beyond-end snapshot and the first partial window.
        frames, hop = 137, 16
        for sample in range(frames):
            weight_sum = sum(
                max(0, 1 - abs(sample - center) / hop)
                for center in range(0, frames + hop, hop)
            )
            self.assertAlmostEqual(weight_sum, 1.0, places=9,
                                   msg=f"sample {sample}")


class CpuRirPhysicsTests(unittest.TestCase):
    def test_real_cpu_rir_direct_delay_distance_and_repeatability(self):
        import numpy as np
        import pyroomacoustics as pra
        from acoustix_scene import rir_snapshot

        # Same source into two receivers at 1 m and 2 m, keeping the
        # reflection paths much longer than the direct path. No FFT mock.
        fs = 16000
        scene = {
            "sample_rate_hz": fs,
            "room": {"dimensions_m": [10., 9., 3.], "rt60_s": 0.18},
            "robot": {
                "trajectory": [{"t_s": 0, "position_m": [4., 4., 1.5],
                                "yaw_deg": 0}],
                "microphones_m": [[0, 0, 0], [1, 0, 0]],
            },
            "sources": [{"id": "pulse", "position_m": [3., 4., 1.5]}],
        }
        rirs = rir_snapshot(scene, 0)
        rirs_again = rir_snapshot(scene, 0)
        self.assertEqual(len(rirs), 2)
        self.assertTrue(np.array_equal(rirs[0][0], rirs_again[0][0]))
        self.assertTrue(np.array_equal(rirs[1][0], rirs_again[1][0]))

        # The pinned pyroomacoustics documentation defines an 81-tap
        # fractional-delay filter with exactly 40 samples of group delay.
        # This is NOT measured/fitted from the observed RIR peaks.
        speed_of_sound = 343.0
        self.assertAlmostEqual(float(pra.constants.get("c")), speed_of_sound)
        filter_length = int(pra.constants.get("frac_delay_length"))
        self.assertEqual(filter_length, 81, "Changed backend needs new calibration review")
        group_delay = filter_length // 2
        predicted_absolute = direct_path_delay_samples(
            scene["robot"]["trajectory"][0]["position_m"], 0,
            scene["sources"][0]["position_m"],
            scene["robot"]["microphones_m"], fs, speed_of_sound, group_delay)
        expected = [round(distance * fs / speed_of_sound) for distance in (1., 2.)]
        observed = []
        direct_peaks = []
        tail_energies = []
        for mic, target in zip(rirs, expected):
            rir = mic[0]
            # Direct arrival before wall reflections; allow an intrinsic
            # backend FIR group delay but never look before causal time.
            start, stop = max(0, target - 4), min(len(rir), target + 96)
            self.assertGreater(stop, start)
            local_peak = int(np.argmax(np.abs(rir[start:stop]))) + start
            observed.append(local_peak)
            direct_peaks.append(float(abs(rir[local_peak])))
            tail = rir[min(len(rir), local_peak + 400):]
            tail_energies.append(float(np.sum(tail.astype(np.float64) ** 2)))
            self.assertGreater(tail_energies[-1], 0.0,
                               "RIR must contain nonzero late reflections")
        # Absolute arrival test: a true input pulse at sample 137 must
        # emerge at sample 137 + (distance/c + 40-sample group delay).
        # Check the production-path convolution, not just the raw RIR.
        pulse_index = 137
        pulse = np.zeros(256, dtype=np.float32)
        pulse[pulse_index] = 1.0
        pulse_arrivals = []
        for mi, mic in enumerate(rirs):
            self.assertLessEqual(abs(observed[mi] - predicted_absolute[mi]), 2.0)
            output = np.convolve(pulse, mic[0])
            begin = pulse_index + int(predicted_absolute[mi]) - 3
            end = begin + 7
            arrival = begin + int(np.argmax(np.abs(output[begin:end])))
            pulse_arrivals.append(arrival)
            self.assertLessEqual(abs(arrival - pulse_index -
                                     predicted_absolute[mi]), 2.0)
        measured_delta = observed[1] - observed[0]
        expected_delta = (2.0 - 1.0) * fs / speed_of_sound
        self.assertLessEqual(abs(measured_delta - expected_delta), 3.0,
                             f"direct relative delay {measured_delta}")
        self.assertGreater(min(direct_peaks), 0.0)
        near_to_far_db = 20 * math.log10(direct_peaks[0] / direct_peaks[1])
        self.assertAlmostEqual(near_to_far_db, 20 * math.log10(2.0),
                               delta=2.5)
        REPORT["cpu_rir"] = {
            "fixture": "static one-source, two-receiver RIR (1 and 2 meters)",
            "backend": "pinned Acoustix CPU PyRoomAcousticsRoom",
            "expected_delta_samples": expected_delta,
            "fractional_delay_filter_taps": filter_length,
            "fractional_delay_group_samples": group_delay,
            "predicted_absolute_arrival_samples": predicted_absolute,
            "observed_convolved_pulse_arrival_samples": pulse_arrivals,
            "convolved_pulse_emission_sample": pulse_index,
            "absolute_arrival_max_tolerance_samples": 2.0,
            "measured_peak_sample_indices": observed,
            "measured_delta_samples": measured_delta,
            "near_over_far_peak_db": near_to_far_db,
            "expected_near_over_far_peak_db": 20 * math.log10(2.0),
            "rir_tail_energies": tail_energies,
            "bit_exact_rir_repeatability": True,
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu-rir", action="store_true",
                        help="Include genuine Acoustix CPU impulse response checks")
    parser.add_argument("--report", type=Path,
                        help="Write S1 check evidence JSON after successful tests")
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(GeometryContractTests)
    if args.cpu_rir:
        suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(CpuRirPhysicsTests))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({
            "schema": "libaudition.acoustix-physics-check.v1",
            "passed": True,
            "geometry_contracts": True,
            "cpu_rir_exercised": args.cpu_rir,
            "evidence": REPORT,
            "not_validated": [
                "frequency-dependent wall absorption",
                "continuous-wave Doppler or moving-microphone phase",
                "measurement-correlated microphone acoustics",
                "real speech WER and source separation",
            ],
        }, indent=2) + "\n")


if __name__ == "__main__":
    main()
