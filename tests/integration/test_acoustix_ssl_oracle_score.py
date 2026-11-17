#!/usr/bin/env python3
"""Independent contract tests for the experimental SSL oracle scorer."""
import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("score", Path(__file__).with_name("acoustix_ssl_oracle_score.py"))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def h(start, status, angle=None):
    return {"start_sample": start, "end_sample": start + 8000,
            "status": status, "peak_azimuth_deg": angle}


def o(start, angles):
    return {"start_sample": start, "end_sample": start + 8000,
            "azimuths_deg": angles}


class ScoreTests(unittest.TestCase):
    def test_two_talkers_single_proposal_is_not_full_recall(self):
        result = mod.score([h(0, "hypothesis", 30)], [o(0, [30, -40])])
        self.assertEqual((result["true_positives"], result["missed_sources"]), (1, 1))
        self.assertEqual(result["recall"], .5)

    def test_silence_false_proposal(self):
        result = mod.score([h(0, "hypothesis", 20)], [o(0, [])])
        self.assertEqual(result["false_proposals"], 1)
        self.assertEqual(result["precision"], 0)
        self.assertIsNone(result["matched_azimuth_p90_deg"])

    def test_correct_silent_negative(self):
        result = mod.score([h(0, "silent")], [o(0, [])])
        self.assertEqual(result["false_proposals"], 0)
        self.assertIsNone(result["recall"])

    def test_mismatched_timing_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "bounds"):
            mod.score([h(0, "silent")], [o(128, [])])

    def test_wraparound(self):
        result = mod.score([h(0, "hypothesis", -179)], [o(0, [179])])
        self.assertEqual(result["matched_azimuth_median_deg"], 2)

    def test_rejects_malformed_numeric_inputs(self):
        for bad in [None, True, "30", float("nan"), float("inf")]:
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    mod.score([h(0, "hypothesis", bad)], [o(0, [30])])
                with self.assertRaises(ValueError):
                    mod.score([h(0, "silent")], [o(0, [bad])])
                with self.assertRaises(ValueError):
                    mod.score([h(0, "silent")], [o(0, [])], tolerance_deg=bad)

    def test_rejects_invalid_window_bounds(self):
        for start, end in [(-1, 8000), (0, 0), (0, True), (False, 8000)]:
            with self.subTest(start=start, end=end):
                hypothesis = h(0, "silent")
                hypothesis.update(start_sample=start, end_sample=end)
                with self.assertRaises(ValueError):
                    mod.score([hypothesis], [o(0, [])])

    def test_overlapping_windows_match_probe_default_half_step(self):
        result = mod.score([h(0, "hypothesis", 30), h(4000, "hypothesis", -40)],
                           [o(0, [30]), o(4000, [-40])])
        self.assertEqual(result["true_positives"], 2)
        self.assertEqual(result["recall"], 1)

    def test_rejects_duplicate_or_unordered_windows(self):
        for starts in [(0, 0), (4000, 0)]:
            with self.subTest(starts=starts):
                with self.assertRaisesRegex(ValueError, "unordered|duplicate"):
                    mod.score([h(x, "silent") for x in starts],
                              [o(x, []) for x in starts])

    def test_rejects_nonlist_oracle(self):
        with self.assertRaisesRegex(ValueError, "list"):
            mod.score([h(0, "silent")], [o(0, "30")])

    def test_unmatched_direction(self):
        result = mod.score([h(0, "hypothesis", 90)], [o(0, [-90])])
        self.assertEqual((result["true_positives"], result["false_proposals"], result["missed_sources"]), (0, 1, 1))


if __name__ == "__main__":
    unittest.main()
