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
                    "unmatched_track_observations_during_voice": 0,
                    "max_unmatched_tracks_during_voice": 0,
                    "max_tracks_during_multi_voice": expected if expected > 1 else 0,
                    "max_unique_oracle_matches_during_multi_voice": expected if expected > 1 else 0,
                    "multi_voice_hops": 10 if expected > 1 else 0,
                    "fully_matched_multi_voice_hops": 10 if expected > 1 else 0,
                    "fully_matched_multi_voice_fraction": 1.0 if expected > 1 else 0.0,
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

    def test_real_single_voice_has_undefined_overlap_fraction(self):
        for variant in VARIANTS:
            mutate(self.root, variant, lambda d: d["cases"]["t02_dev_single"]["metrics"].update(
                fully_matched_multi_voice_fraction=None))
        report = evaluate(self.root)
        self.assertIsNone(report["cases"]["t02_dev_single"]["feature_capacity_deficit"])
        self.assertEqual(report["cases"]["t02_dev_single"]["status"], "NOT_APPLICABLE_SINGLE_SPEAKER")

    def test_undefined_fraction_rejected_when_overlap_exists(self):
        mutate(self.root, "dev", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(
            fully_matched_multi_voice_fraction=None))
        with self.assertRaisesRegex(ValueError, "invalid native metric"):
            evaluate(self.root)

    def test_multi_speaker_oracle_without_overlap_rejected(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(
            multi_voice_hops=0, fully_matched_multi_voice_hops=0,
            fully_matched_multi_voice_fraction=None))
        with self.assertRaisesRegex(ValueError, "missing oracle overlap"):
            evaluate(self.root)

    def test_overlap_fraction_must_agree_with_native_hop_counts(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t07_eval_seven"]["metrics"].update(
            fully_matched_multi_voice_hops=4, fully_matched_multi_voice_fraction=0.9))
        with self.assertRaisesRegex(ValueError, "inconsistent overlap fraction"):
            evaluate(self.root)

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

    def test_raw_capacity_cannot_mask_failed_oracle_matching(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(
            max_simultaneous_tracks=7,
            max_tracks_during_multi_voice=7,
            max_unique_oracle_matches_during_multi_voice=1,
            fully_matched_multi_voice_hops=0,
            fully_matched_multi_voice_fraction=0.0))
        result = evaluate(self.root)
        self.assertEqual(result["cases"]["t03_eval_two"]["feature_capacity_deficit"], 1)
        self.assertIn("1:1 overlap capacity worse than dev",
                      result["cases"]["t03_eval_two"]["regression_flags"])

    def test_true_track_capacity_deficit_is_distinct(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(
            max_simultaneous_tracks=1,
            max_tracks_during_multi_voice=1,
            max_unique_oracle_matches_during_multi_voice=1,
            fully_matched_multi_voice_hops=0,
            fully_matched_multi_voice_fraction=0.0))
        result = evaluate(self.root)
        self.assertEqual(result["cases"]["t03_eval_two"]["status"], "TRACK_CAPACITY_DEFICIT")

    def test_single_speaker_ghost_track_regression_is_blocked(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t02_dev_single"]["metrics"].update(
            max_simultaneous_tracks=2,
            unmatched_track_observations_during_voice=12,
            max_unmatched_tracks_during_voice=1))
        report = evaluate(self.root)
        single = report["cases"]["t02_dev_single"]
        self.assertEqual(single["single_voice_ghost_track_observations"], 12)
        self.assertIn("unmatched single-voice tracks worse than dev", single["regression_flags"])
        self.assertIn("t02_dev_single: unmatched single-voice tracks worse than upstream",
                      report["quality_blockers"])

    def test_multi_speaker_ghost_regression_is_flagged_even_with_good_coverage(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(
            matched_voice_source_fraction=0.95,
            max_simultaneous_tracks=3,
            unmatched_track_observations_during_voice=25,
            max_unmatched_tracks_during_voice=1))
        report = evaluate(self.root)
        flags = report["cases"]["t03_eval_two"]["regression_flags"]
        self.assertIn("unmatched track observations worse than dev", flags)
        self.assertIn("unmatched track observations worse than upstream", flags)
        self.assertIn("t03_eval_two: unmatched track observations worse than dev",
                      report["quality_blockers"])

    def test_matched_id_switch_regression_is_flagged_independently(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t07_eval_seven"]["metrics"].update(
            adjacent_matched_id_switches=2))
        report = evaluate(self.root)
        flags = report["cases"]["t07_eval_seven"]["regression_flags"]
        self.assertIn("matched ID switches worse than dev", flags)
        self.assertIn("matched ID switches worse than upstream", flags)

    def test_equal_ghost_and_id_switch_counts_do_not_trigger_regression(self):
        for variant in VARIANTS:
            mutate(self.root, variant, lambda d: d["cases"]["t03_eval_two"]["metrics"].update(
                max_simultaneous_tracks=3,
                unmatched_track_observations_during_voice=5,
                max_unmatched_tracks_during_voice=1,
                adjacent_matched_id_switches=1))
        flags = evaluate(self.root)["cases"]["t03_eval_two"]["regression_flags"]
        self.assertFalse(any("unmatched track observations worse" in flag or
                             "matched ID switches worse" in flag for flag in flags))

    def test_unmatched_track_count_cannot_exceed_capacity(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t02_dev_single"]["metrics"].update(
            max_simultaneous_tracks=1,
            max_unmatched_tracks_during_voice=2))
        with self.assertRaisesRegex(ValueError, "unmatched tracks exceed track capacity"):
            evaluate(self.root)

    def test_no_extra_tracks_remains_unflagged(self):
        report = evaluate(self.root)
        self.assertEqual(report["cases"]["t02_dev_single"]["single_voice_ghost_track_observations"], 0)
        self.assertFalse(any("unmatched single-voice" in flag
                             for flag in report["quality_blockers"]))

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

    def test_fractional_overlap_counts_rejected(self):
        for key in ("max_tracks_during_multi_voice",
                    "max_unique_oracle_matches_during_multi_voice",
                    "multi_voice_hops", "fully_matched_multi_voice_hops"):
            with self.subTest(metric=key):
                mutate(self.root, "feature", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(
                    {key: 1.5}))
                with self.assertRaisesRegex(ValueError, "invalid tracking count"):
                    evaluate(self.root)
                fixture_root = self.root / "odas-ab-feature" / "ab_result.json"
                data = json.loads(fixture_root.read_text())
                data["cases"]["t03_eval_two"]["metrics"][key] = {
                    "max_tracks_during_multi_voice": 2,
                    "max_unique_oracle_matches_during_multi_voice": 2,
                    "multi_voice_hops": 10,
                    "fully_matched_multi_voice_hops": 10,
                }[key]
                fixture_root.write_text(json.dumps(data))

    def test_negative_overlap_count_rejected(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(
            multi_voice_hops=-1))
        with self.assertRaisesRegex(ValueError, "invalid tracking count"):
            evaluate(self.root)

    def test_coverage_over_one_rejected(self):
        mutate(self.root, "upstream", lambda d: d["cases"]["t02_dev_single"]["metrics"].update(matched_voice_source_fraction=1.01))
        with self.assertRaisesRegex(ValueError, "invalid coverage"):
            evaluate(self.root)

    def test_deficit_is_not_silently_passed(self):
        mutate(self.root, "feature", lambda d: d["cases"]["t03_eval_two"]["metrics"].update(max_unique_oracle_matches_during_multi_voice=1))
        result = evaluate(self.root)
        self.assertEqual(result["cases"]["t03_eval_two"]["feature_capacity_deficit"], 1)
        self.assertEqual(result["cases"]["t03_eval_two"]["status"], "ORACLE_MATCH_DEFICIT")
        self.assertIn("T03: fewer than two simultaneous 1:1 oracle-matched tracks", result["quality_blockers"])


if __name__ == "__main__":
    unittest.main()
