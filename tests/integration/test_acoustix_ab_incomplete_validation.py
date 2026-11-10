#!/usr/bin/env python3
"""Reject invalid measured values even when the other A/B arm is absent."""
import unittest

from acoustix_spatial_paired_ab import compare
from acoustix_odas_three_way_ab import evaluate

BASE = {
    "input_wav_sha256": "a" * 64,
    "oracle_sha256": "b" * 64,
    "configuration_sha256": "c" * 64,
    "case_id": "T03",
    "input_frames": 192000,
    "sample_rate_hz": 16000,
}


class IncompleteInputValidationTests(unittest.TestCase):
    def test_nan_not_masked_by_missing_baseline(self):
        with self.assertRaisesRegex(ValueError, "non-finite"):
            compare(BASE, {**BASE, "ssl": {"candidate_recall": float("nan")}})

    def test_boolean_not_masked_by_missing_baseline(self):
        with self.assertRaisesRegex(ValueError, "non-numeric"):
            compare(BASE, {**BASE, "sst": {"id_switches": True}})

    def test_out_of_range_not_masked_by_missing_baseline(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            compare(BASE, {**BASE, "sst": {"idf1": 1.2}})

    def test_negative_latency_not_masked_by_missing_baseline(self):
        with self.assertRaisesRegex(ValueError, "negative"):
            compare(BASE, {**BASE, "sst": {"birth_latency_ms": -1}})

    def test_invalid_event_count_not_masked_by_missing_baseline(self):
        with self.assertRaisesRegex(ValueError, "event count"):
            compare(BASE, {**BASE, "sst": {"false_tracks": 0.5}})

    def test_valid_partial_stays_unmeasured(self):
        report = compare(BASE, {**BASE, "ssl": {"candidate_recall": 0.5}})
        self.assertFalse(report["complete"])
        self.assertEqual(report["metrics"]["ssl"]["candidate_recall"]["status"], "UNMEASURED")

    def test_three_way_invalid_feature_rejected_even_without_baseline_metrics(self):
        rows = {
            role: {**BASE, "odas_repository": "ARDev1161/odas",
                   "odas_commit_sha": digit * 40}
            for role, digit in (("upstream", "1"), ("dev", "2"), ("feature", "3"))
        }
        rows["feature"]["sss"] = {"si_sdri_db": float("inf")}
        with self.assertRaisesRegex(ValueError, "non-finite"):
            evaluate(rows)


if __name__ == "__main__":
    unittest.main()
