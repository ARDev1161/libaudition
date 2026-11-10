#!/usr/bin/env python3
"""Deterministic contract tests, not a substitute for real ODAS WAV replays."""
import importlib.util
from pathlib import Path
import unittest

path=Path(__file__).with_name("acoustix_odas_three_way_ab.py")
spec=importlib.util.spec_from_file_location("threeway",path)
threeway=importlib.util.module_from_spec(spec)
spec.loader.exec_module(threeway)


def fixtures():
    base={"input_wav_sha256":"a"*64,"oracle_sha256":"b"*64,
          "configuration_sha256":"c"*64,"case_id":"T03",
          "input_frames":192000,"sample_rate_hz":16000,
          "odas_repository":"ARDev1161/odas"}
    return {r:{**base,"odas_commit_sha":c*40} for r,c in
            (("upstream","1"),("dev","2"),("feature","3"))}


class ThreeWayTests(unittest.TestCase):
    def test_missing_sss_blocks_acceptance(self):
        result=threeway.evaluate(fixtures())
        self.assertEqual(result["status"],"BLOCKED_MISSING_METRICS")
        self.assertFalse(result["quality_accepted"])

    def test_changed_wav_fails_closed(self):
        rows=fixtures()
        rows["feature"]["input_wav_sha256"]="f"*64
        with self.assertRaisesRegex(ValueError,"input_wav_sha256"):
            threeway.evaluate(rows)

    def test_changed_oracle_fails_closed(self):
        rows=fixtures()
        rows["feature"]["oracle_sha256"]="f"*64
        with self.assertRaisesRegex(ValueError,"oracle_sha256"):
            threeway.evaluate(rows)

    def test_changed_config_fails_closed(self):
        rows=fixtures()
        rows["feature"]["configuration_sha256"]="f"*64
        with self.assertRaisesRegex(ValueError,"configuration_sha256"):
            threeway.evaluate(rows)

    def test_invalid_hashes_fail_before_comparison(self):
        for key in ("input_wav_sha256", "oracle_sha256", "configuration_sha256"):
            for invalid in ("", "not-a-digest", "A"*64, "g"*64, None):
                with self.subTest(key=key, invalid=invalid):
                    rows=fixtures()
                    rows["feature"][key]=invalid
                    with self.assertRaisesRegex(ValueError, key):
                        threeway.evaluate(rows)

    def test_invalid_replay_dimensions_fail_before_comparison(self):
        for key in ("input_frames", "sample_rate_hz"):
            for invalid in (0, -1, 1.5, True, "16000", None):
                with self.subTest(key=key, invalid=invalid):
                    rows=fixtures()
                    rows["feature"][key]=invalid
                    with self.assertRaisesRegex(ValueError, key):
                        threeway.evaluate(rows)

    def test_blank_case_id_rejected(self):
        rows=fixtures()
        rows["feature"]["case_id"]="  "
        with self.assertRaisesRegex(ValueError, "case_id"):
            threeway.evaluate(rows)

    def test_same_dev_feature_sha_rejected(self):
        rows=fixtures()
        rows["feature"]["odas_commit_sha"]=rows["dev"]["odas_commit_sha"]
        with self.assertRaisesRegex(ValueError,"distinct"):
            threeway.evaluate(rows)

    def test_complete_metrics_detect_regression(self):
        rows=fixtures()
        for row in rows.values():
            for group,names in threeway.METRICS.items():
                row[group]={name:0 for name in names}
        rows["feature"]["sst"]["source_recall"]=0.5
        rows["dev"]["sst"]["source_recall"]=0.8
        result=threeway.evaluate(rows)
        self.assertEqual(result["status"],"REGRESSIONS_VS_DEV")
        self.assertIn("sst.source_recall",result["comparisons"]["dev_to_feature"]["regressions"])

    def test_complete_equal_not_quality_acceptance(self):
        rows=fixtures()
        for row in rows.values():
            for group,names in threeway.METRICS.items():
                row[group]={name:0 for name in names}
        result=threeway.evaluate(rows)
        self.assertEqual(result["status"],"MEASURED_NO_REGRESSIONS_VS_DEV")
        self.assertFalse(result["quality_accepted"])


if __name__=="__main__":
    unittest.main()
