#!/usr/bin/env python3
"""Independent negative/positive contracts for the spatial paired A/B tool."""
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).with_name("acoustix_spatial_paired_ab.py")
spec = importlib.util.spec_from_file_location("spatial_ab", path)
ab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ab)


def fixture():
    return dict(input_wav_sha256="a" * 64, oracle_sha256="b" * 64,
                configuration_sha256="c" * 64, case_id="T03",
                input_frames=30000, sample_rate_hz=16000)


class SpatialPairedABTests(unittest.TestCase):
    def test_missing_sss_cannot_pass(self):
        result = ab.compare(fixture(), fixture())
        self.assertEqual(result["status"], "INCOMPLETE")
        self.assertEqual(result["metrics"]["sss"]["si_sdri_db"]["status"], "UNMEASURED")

    def test_different_wav_rejected(self):
        a, b = fixture(), fixture()
        b["input_wav_sha256"] = "d" * 64
        with self.assertRaisesRegex(ValueError, "input_wav_sha256"):
            ab.compare(a, b)

    def test_different_geometry_options_rejected(self):
        a, b = fixture(), fixture()
        b["configuration_sha256"] = "d" * 64
        with self.assertRaisesRegex(ValueError, "configuration_sha256"):
            ab.compare(a, b)

    def test_different_oracle_rejected(self):
        a, b = fixture(), fixture()
        b["oracle_sha256"] = "d" * 64
        with self.assertRaisesRegex(ValueError, "oracle_sha256"):
            ab.compare(a, b)

    def test_improved_doa_but_worse_leakage_detected(self):
        a, b = fixture(), fixture()
        a["ssl"] = {"azimuth_median_deg": 20}
        b["ssl"] = {"azimuth_median_deg": 10}
        a["sss"] = {"interferer_leakage_db": -12}
        b["sss"] = {"interferer_leakage_db": -5}
        result = ab.compare(a, b)
        self.assertEqual(result["metrics"]["ssl"]["azimuth_median_deg"]["status"], "IMPROVED")
        self.assertIn("sss.interferer_leakage_db", result["regressions"])

    def test_nonfinite_quality_rejected(self):
        a, b = fixture(), fixture()
        a["sst"], b["sst"] = {"idf1": float("nan")}, {"idf1": 0.5}
        with self.assertRaisesRegex(ValueError, "non-finite"):
            ab.compare(a, b)

    def test_bad_hash_rejected(self):
        a, b = fixture(), fixture()
        a["input_wav_sha256"] = "not-a-real-hash"
        with self.assertRaisesRegex(ValueError, "invalid SHA256"):
            ab.compare(a, b)

    def test_invalid_sample_clock_rejected(self):
        a, b = fixture(), fixture()
        b["sample_rate_hz"] = True
        with self.assertRaisesRegex(ValueError, "invalid sample clock"):
            ab.compare(a, b)

    def test_recall_over_one_rejected(self):
        a, b = fixture(), fixture()
        a["ssl"], b["ssl"] = {"candidate_recall": 0.5}, {"candidate_recall": 1.2}
        with self.assertRaisesRegex(ValueError, "outside"):
            ab.compare(a, b)

    def test_negative_switch_count_rejected(self):
        a, b = fixture(), fixture()
        a["sst"], b["sst"] = {"id_switches": 1}, {"id_switches": -1}
        with self.assertRaisesRegex(ValueError, "event count"):
            ab.compare(a, b)

    def test_all_measured_equal(self):
        a, b = fixture(), fixture()
        for group, names in ab.METRICS.items():
            a[group] = {name: 0 for name in names}
            b[group] = dict(a[group])
        self.assertEqual(ab.compare(a, b)["status"], "NO_REGRESSIONS")


if __name__ == "__main__":
    unittest.main()
