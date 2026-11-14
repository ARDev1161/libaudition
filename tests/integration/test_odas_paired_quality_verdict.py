#!/usr/bin/env python3
"""Independent negative and positive contracts for paired ODAS quality evidence."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from odas_paired_quality_verdict import CASES, VARIANTS, EXPECTED_SHAS, evaluate


def fixture(root):
    for variant in VARIANTS:
        cases = {}
        for case, expected in CASES.items():
            cases[case] = {
                "wav_sha256": "a" * 64,
                "oracle_sha256": "b" * 64,
                "metrics": {
                    "matched_voice_source_fraction": 0.75,
                    "matched_p90_azimuth_error_deg": 8.0,
                    "max_simultaneous_tracks": expected,
                    "adjacent_matched_id_switches": 0,
                },
            }
        p = root / ("odas-ab-" + variant)
        p.mkdir()
        (p / "ab_result.json").write_text(json.dumps({
            "variant": variant, "odas_sha": EXPECTED_SHAS[variant], "cases": cases
        }))


def mutate(root, variant, callback):
    p = root / ("odas-ab-" + variant) / "ab_result.json"
    data = json.loads(p.read_text())
    callback(data)
    p.write_text(json.dumps(data))


class QualityVerdictTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        fixture(self.root)

    def test_valid_matrix_remains_quality_blocked_without_sss(self):
        result = evaluate(self.root)
        self.assertEqual(result["quality_status"], "BLOCKED")
        self.assertEqual(result["cases"]["t03_eval_two"]["feature_capacity_deficit"], 0)
        self.assertTrue(any("SI-SDRi" in x for x in result["quality_blockers"]))

    def test_wrong_odas_revision_rejected(self):
        mutate(self.root, "dev", lambda d: d.update(odas_sha="0" * 40))
        with self.assertRaisesRegex(ValueError, "revision provenance"):
            evaluate(self.root)

    def test_feature_azimuth_regression_is_flagged(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(matched_p90_azimuth_error_deg=12.0))
        result = evaluate(self.root)
        self.assertIn("P90 azimuth worse than dev", result["cases"]["t03_eval_two"]["regression_flags"])
        self.assertIn("t03_eval_two: P90 azimuth worse than upstream", result["quality_blockers"])

    def test_feature_coverage_regression_is_flagged(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t02_dev_single"]["metrics"].update(matched_voice_source_fraction=0.6))
        result = evaluate(self.root)
        self.assertIn("coverage worse than dev", result["cases"]["t02_dev_single"]["regression_flags"])

    def test_missing_variant_rejected(self):
        (self.root / "odas-ab-dev" / "ab_result.json").unlink()
        with self.assertRaisesRegex(ValueError, "missing real-run"):
            evaluate(self.root)

    def test_missing_case_rejected(self):
        mutate(self.root, "feature", lambda d: d["cases"].pop("t07_eval_seven"))
        with self.assertRaisesRegex(ValueError, "incomplete case"):
            evaluate(self.root)

    def test_wav_mismatch_rejected(self):
        mutate(self.root, "dev", lambda d: d["cases"]["t03_eval_two"].update(wav_sha256="c" * 64))
        with self.assertRaisesRegex(ValueError, "input/oracle mismatch"):
            evaluate(self.root)

    def test_oracle_mismatch_rejected(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t02_dev_single"].update(oracle_sha256="d" * 64))
        with self.assertRaisesRegex(ValueError, "input/oracle mismatch"):
            evaluate(self.root)

    def test_nonfinite_metric_rejected(self):
        mutate(self.root, "dev", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(matched_p90_azimuth_error_deg=float("nan")))
        with self.assertRaisesRegex(ValueError, "invalid native metric"):
            evaluate(self.root)

    def test_negative_count_rejected(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t07_eval_seven"]["metrics"].update(max_simultaneous_tracks=-1))
        with self.assertRaisesRegex(ValueError, "invalid tracking count"):
            evaluate(self.root)

    def test_coverage_over_one_rejected(self):
        mutate(self.root, "upstream", lambda d: d["cases"]["t02_dev_single"]["metrics"].update(matched_voice_source_fraction=1.01))
        with self.assertRaisesRegex(ValueError, "invalid coverage"):
            evaluate(self.root)

    def test_deficit_is_not_silently_passed(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(max_simultaneous_tracks=1))
        result = evaluate(self.root)
        self.assertEqual(result["cases"]["t03_eval_two"]["feature_capacity_deficit"], 1)
        self.assertEqual(result["cases"]["t03_eval_two"]["status"], "CAPACITY_DEFICIT")
        self.assertIn("T03: fewer than two simultaneous tracks", result["quality_blockers"])


if __name__ == "__main__":
    unittest.main()
