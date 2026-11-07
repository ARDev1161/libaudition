#!/usr/bin/env python3
"""S1 audit of RT60 over source/microphone positions, with pinned real CPU RIR.

A single-reference RIR can calibrate the room-wide nominal RT60 while other
positions disagree. This test separately measures that spatial variability;
it never estimates RT60 from ODAS tracks or transcripts and never recalibrates
a different model for every source/microphone.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import statistics
import unittest
from pathlib import Path

from acoustix_scene import check_scene, rir_snapshot
from acoustix_reverb import calibrate_scene_rt60, estimate_rt60_t20

FIXTURE = (Path(__file__).resolve().parents[1] /
           "fixtures/acoustix/table_conversation_7x7.json")
REPORT: dict = {}


class SpatialRt60Tests(unittest.TestCase):
    def test_roomwide_decay_with_one_fixed_calibration(self):
        scene = json.loads(FIXTURE.read_text(encoding="utf-8"))
        scene["sources"] = [copy.deepcopy(src) for src in scene["sources"]
                            if src["id"] in {"speaker_1", "speaker_2", "speaker_3"}]
        check_scene(scene)

        requested = float(scene["room"]["rt60_s"])
        calibrated, calibration = calibrate_scene_rt60(scene)
        self.assertLessEqual(abs(calibration["calibrated_relative_error"]), 0.05)

        measurements = []
        positions = (0.0, 4.5, 9.0)
        for t in positions:
            rirs = rir_snapshot(calibrated, t)
            self.assertEqual(len(rirs), 4)
            for mi, mic in enumerate(rirs):
                self.assertEqual(len(mic), len(scene["sources"]))
                for si, source in enumerate(scene["sources"]):
                    base = {
                        "robot_time_s": t,
                        "microphone_index": mi,
                        "source_id": source["id"],
                        "rir_length_samples": len(mic[si]),
                    }
                    try:
                        measured = estimate_rt60_t20(
                            mic[si], scene["sample_rate_hz"])
                        base["measured_t60_s"] = measured["t60_s"]
                        base["relative_error"] = (
                            measured["t60_s"] - requested) / requested
                        base["fit_r_squared"] = measured["r_squared"]
                        base["credible_t20_fit"] = measured["r_squared"] >= 0.85
                    except ValueError as exc:
                        base["credible_t20_fit"] = False
                        base["error"] = str(exc)
                    measurements.append(base)

        credible = [m for m in measurements if m["credible_t20_fit"]]
        deviations = sorted(abs(m["relative_error"]) for m in credible)
        measured_rt60 = sorted(m["measured_t60_s"] for m in credible)
        count = len(measurements)
        result = {
            "requested_room_rt60_s": requested,
            "fixed_effective_backend_rt60_s":
                calibration["effective_backend_rt60_s"],
            "fixed_reference_probe_t60_s": calibration["calibrated_probe_rt60_s"],
            "robot_pose_times_s": list(positions),
            "sources_per_pose": len(scene["sources"]),
            "microphones_per_pose": len(scene["robot"]["microphones_m"]),
            "rir_count": count,
            "credible_fit_count": len(credible),
            "credible_fit_fraction": len(credible) / count,
            "individual_rirs": measurements,
            "metric": "Schroeder T20, -5..-25dB energy decay",
        }
        if credible:
            p90_index = min(len(deviations) - 1,
                            math.ceil(0.9 * len(deviations)) - 1)
            result.update({
                "median_measured_rt60_s": statistics.median(measured_rt60),
                "median_absolute_relative_error": statistics.median(deviations),
                "p90_absolute_relative_error": deviations[p90_index],
                "min_measured_rt60_s": measured_rt60[0],
                "max_measured_rt60_s": measured_rt60[-1],
            })
        REPORT["spatial_rt60"] = result

        # The reference-probe quality already passed independently. This
        # suite's room-wide gate is predeclared BEFORE reading results.
        self.assertGreaterEqual(
            result["credible_fit_fraction"], 0.80,
            "Too few source/mic/pose RIRs have a credible T20 decay fit")
        self.assertLessEqual(
            result["median_absolute_relative_error"], 0.25,
            "Room-wide median RT60 differs from specified acoustic target")
        self.assertLessEqual(
            result["p90_absolute_relative_error"], 0.50,
            "Severe spatial RT60 deviation in high-percentile source/mic paths")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SpatialRt60Tests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({
            "schema": "libaudition.acoustix-spatial-rt60.v1",
            "passed": result.wasSuccessful(),
            "evidence": REPORT,
            "limitations": [
                "single room and one calibration microphone; 36 RIRs",
                "finite-order rectangular image-source model",
                "not measured hardware or frequency-dependent absorption",
                "metric applies only where T20 fit has R²>=0.85",
            ],
        }, indent=2) + "\n")
    if not result.wasSuccessful():
        raise SystemExit(1)


if __name__ == "__main__":
    main()
