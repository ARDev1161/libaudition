"""Contract-only tests of the paired native source-evidence checker."""
import json
from pathlib import Path
import tempfile
import unittest

from acoustix_s2_zero_evidence_birth_compare import (
    ComparisonError, _assert_no_zero_strength_births,
)


def create_trace(root, strength, posterior, births):
    folder = Path(root) / "gmm-sweep/active_var_001/t00_zero_pcm_control"
    folder.mkdir(parents=True)
    p = folder / "sst_raw_trace.jsonl"
    with p.open("w") as target:
        for i in range(1875):
            record = {
                "schema": "odas_sst_raw_v1",
                "hop_index": i, "sample_offset": i * 128,
                "odas_timestamp": i + 1, "theta_new": 0.9,
                "ssl_candidates": [{
                    "index": 0, "strength": strength,
                    "birth_posterior": posterior}],
                "births": [2] if i == 0 and births else [],
                "retirements": [],
            }
            target.write(json.dumps(record) + "\n")
    return p


class PositiveEvidenceContracts(unittest.TestCase):
    def test_native_wide_gmm_empty_pot_birth_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            create_trace(directory, 0.0, 0.978264868, True)
            with self.assertRaisesRegex(ComparisonError, "without positive"):
                _assert_no_zero_strength_births(
                    directory, "active_var_001_off", "t00_zero_pcm_control")

    def test_zero_pot_with_no_birth_is_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            create_trace(directory, 0.0, 0.978264868, False)
            result = _assert_no_zero_strength_births(
                directory, "active_var_001_off", "t00_zero_pcm_control")
            self.assertEqual(result["native_hops_validated"], 1875)

    def test_positive_pot_can_still_birth(self):
        with tempfile.TemporaryDirectory() as directory:
            create_trace(directory, 0.25, 0.95, True)
            result = _assert_no_zero_strength_births(
                directory, "active_var_001_off", "t00_zero_pcm_control")
            self.assertTrue(result["births_only_with_positive_ssl_evidence"])

    def test_broken_sst_clock_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = create_trace(directory, 0.1, 0.95, False)
            rows = trace.read_text().splitlines()
            row = json.loads(rows[4])
            row["sample_offset"] = 999
            rows[4] = json.dumps(row)
            trace.write_text("\n".join(rows) + "\n")
            with self.assertRaisesRegex(ComparisonError, "clock"):
                _assert_no_zero_strength_births(
                    directory, "active_var_001_off", "t00_zero_pcm_control")

    def test_truncated_native_trace_not_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = create_trace(directory, 0.1, 0.95, False)
            trace.write_text(trace.read_text().splitlines()[0] + "\n")
            with self.assertRaisesRegex(ComparisonError, "hop count"):
                _assert_no_zero_strength_births(
                    directory, "active_var_001_off", "t00_zero_pcm_control")


if __name__ == "__main__":
    unittest.main()
