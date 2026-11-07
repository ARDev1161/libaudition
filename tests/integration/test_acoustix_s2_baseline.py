"""Adversarial tests for frozen real-speech ODAS diagnostic comparison."""
import copy
import json
from pathlib import Path
import unittest

from acoustix_s2_baseline import compare

BASELINE = Path(__file__).resolve().parents[1] / "fixtures" / "acoustix" / "s2_real_odas_baseline.json"


class BaselineTests(unittest.TestCase):
    def setUp(self):
        self.baseline = json.loads(BASELINE.read_text())
        self.report = {
            "schema": "libaudition.acoustix-s2-real-odas-diagnostics.v1",
            "cases": [
                dict(case, passed_pipeline_integrity=True)
                for case in self.baseline["cases"]
            ],
        }

    def test_exact_baseline_passes_without_quality_claims(self):
        result = compare(self.baseline, self.report)
        self.assertTrue(result["regression_passed"])
        self.assertFalse(result["separation_validated"])
        self.assertFalse(result["asr_validated"])

    def test_missing_or_duplicate_case_rejected(self):
        self.report["cases"].pop()
        with self.assertRaises(ValueError):
            compare(self.baseline, self.report)
        self.report["cases"] = [self.report["cases"][0]] * 3
        with self.assertRaises(ValueError):
            compare(self.baseline, self.report)

    def test_changed_input_or_oracle_rejected(self):
        for field, value in (("input_sha256", "0" * 64),
                             ("oracle_voice_source_hops", 1)):
            report = copy.deepcopy(self.report)
            report["cases"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                compare(self.baseline, report)

    def test_recall_regression_and_id_switch_are_visible(self):
        self.report["cases"][1]["matched_voice_source_hops"] = 100
        self.report["cases"][2]["adjacent_id_switches"] = 1
        result = compare(self.baseline, self.report)
        self.assertFalse(result["regression_passed"])
        self.assertIn("t03_eval_two:source_hop_recall_not_regressed",
                      result["regressions"])
        self.assertIn("t07_eval_seven:id_switches_not_increased",
                      result["regressions"])

    def test_impossible_or_malformed_observations_rejected(self):
        for field, value in (
            ("matched_voice_source_hops", -1),
            ("matched_voice_source_hops", 100000),
            ("matched_voice_source_hops", True),
            ("adjacent_id_switches", -1),
            ("recorder_drops", -1),
            ("max_simultaneous_tracks", -1),
            ("max_simultaneous_tracks", 1.5),
        ):
            report = copy.deepcopy(self.report)
            report["cases"][0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                compare(self.baseline, report)

    def test_pipeline_integrity_failure_is_regression(self):
        self.report["cases"][0]["passed_pipeline_integrity"] = False
        result = compare(self.baseline, self.report)
        self.assertFalse(result["regression_passed"])
        self.assertIn("t02_dev_single:pipeline_integrity", result["regressions"])

    def test_track_capacity_collapse_is_reported(self):
        report = copy.deepcopy(self.report)
        candidate = next(case for case in report["cases"]
                         if case["max_simultaneous_tracks"] > 0)
        candidate["max_simultaneous_tracks"] = 0
        result = compare(self.baseline, report)
        self.assertFalse(result["regression_passed"])
        self.assertIn(candidate["case"] + ":track_capacity_not_regressed",
                      result["regressions"])

    def test_corrupted_baseline_is_rejected(self):
        for field, value in (("matched_voice_source_hops", -1),
                             ("max_simultaneous_tracks", True),
                             ("oracle_voice_source_hops", True)):
            baseline = copy.deepcopy(self.baseline)
            baseline["cases"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                compare(baseline, self.report)

    def test_recorder_drop_fails_even_with_good_tracking(self):
        self.report["cases"][0]["recorder_drops"] = 1
        self.assertIn("t02_dev_single:no_recorder_drops",
                      compare(self.baseline, self.report)["regressions"])


if __name__ == "__main__":
    unittest.main()
