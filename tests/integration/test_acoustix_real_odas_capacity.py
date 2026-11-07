#!/usr/bin/env python3
"""Pure-stdlib ODAS real speech capacity-matrix metric contracts."""
import copy
import unittest

from acoustix_real_odas_capacity import (
    CONFIGS, SCENES, proposal_signature, summarize_capacity
)


class RealCapacityContracts(unittest.TestCase):
    def test_frozen_factorial_and_isolation_comparison(self):
        self.assertEqual({name for name, *_ in CONFIGS},
                         {"4x4", "4x8", "8x4", "8x8", "8x8_sst4"})
        self.assertEqual(SCENES, ("t03_eval_two", "t07_eval_seven"))
        obs = [
            {"kind": "proposal", "hop": "0", "id": "1",
             "x": "1", "y": "0", "z": "0", "activity": "0.7"},
            {"kind": "track", "hop": "0", "id": "8", "x": "1", "y": "0", "z": "0"},
        ]
        other = copy.deepcopy(obs)
        other[1]["id"] = "9"
        self.assertEqual(proposal_signature(obs), proposal_signature(other),
                         "Different downstream SST tracks must not affect raw SSL")
        other[0]["activity"] = "0.4"
        self.assertNotEqual(proposal_signature(obs), proposal_signature(other))

    def test_unique_track_match_metrics_and_no_silent_accuracy_clamp(self):
        gt = {"ground_truth": [{"hop": 0, "sources": [
            {"id": "speaker_1", "kind": "voice",
             "bearing_deg": 0.0, "elevation_deg": 0.0},
            {"id": "speaker_2", "kind": "voice",
             "bearing_deg": 130.0, "elevation_deg": 0.0},
        ]}]}
        obs = [{"kind": "proposal", "hop": "0", "id": "5",
                "x": "1", "y": "0", "z": "0", "activity": "0.5"},
               {"kind": "track", "hop": "0", "id": "7",
                "x": "1", "y": "0", "z": "0"}]
        summary = {
            "ssl_potential_source_count": 8, "sst_max_tracks": 8,
            "sst_input_potential_limit": 0,
            "track_observations": 1, "potential_observations": 1,
            "separated_frames": 128, "recordings": 1,
            "recorder_drops": 0, "input_frames": 128,
        }
        report = summarize_capacity(obs, summary, gt)
        self.assertEqual(report["max_simultaneous_tracks"], 1)
        self.assertEqual(report["oracle_active_voice_source_hops"], 2)
        self.assertEqual(report["matched_voice_source_hops"], 1)
        self.assertAlmostEqual(report["matched_voice_source_fraction"], 0.5)
        self.assertEqual(report["matched_azimuth_median_deg"], 0.0)

    def test_unmatched_source_is_not_hidden(self):
        gt = {"ground_truth": [{"hop": 0, "sources": [
            {"id": "speaker_1", "kind": "voice",
             "bearing_deg": 0.0, "elevation_deg": 0.0}
        ]}]}
        summary = {
            "ssl_potential_source_count": 4, "sst_max_tracks": 4,
            "sst_input_potential_limit": 0,
            "track_observations": 0, "potential_observations": 0,
            "separated_frames": 128, "recordings": 0,
            "recorder_drops": 0, "input_frames": 128,
        }
        report = summarize_capacity([], summary, gt)
        self.assertEqual(report["matched_voice_source_fraction"], 0)
        self.assertIsNone(report["matched_azimuth_median_deg"])


if __name__ == "__main__":
    unittest.main()
