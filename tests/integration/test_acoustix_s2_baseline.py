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

    def test_recorder_drop_fails_even_with_good_tracking(self):
        self.report["cases"][0]["recorder_drops"] = 1
        self.assertIn("t02_dev_single:no_recorder_drops",
                      compare(self.baseline, self.report)["regressions"])


if __name__ == "__main__":
    unittest.main()
