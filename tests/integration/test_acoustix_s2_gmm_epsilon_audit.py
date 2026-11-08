"""Strict unit contracts for the ODAS GMM/epsilon analyzer, not acoustic tests."""
import math
import unittest

from acoustix_s2_gmm_epsilon_audit import (
    AuditError, decompose_candidate, gaussian_log_odds, summarize_case)


def candidate(strength, birth, track_mass=0):
    tracks = [{"track_id": 5, "posterior": track_mass}] if track_mass else []
    return {"index": 0, "strength": strength, "birth_posterior": birth,
            "track_posteriors": tracks}


def record(hop=0, potentials=None):
    return {"schema": "odas_sst_raw_v1", "odas_timestamp": hop + 1,
            "hop_index": hop, "sample_offset": hop * 128,
            "hop_frames": 128, "ssl_candidates": potentials or []}


class NativeGmmEvidenceTests(unittest.TestCase):
    def test_native_gaussian_equal_priors_analytic_log_odds(self):
        for s in (0.0, 0.1, 0.25, 0.325, 0.4, 0.7, 1.0):
            self.assertAlmostEqual(gaussian_log_odds(s), 60*s-19.5, places=11)

    def test_equal_false_and_birth_odds_crossing(self):
        self.assertAlmostEqual(gaussian_log_odds(0.325), 0, places=11)

    def test_low_strength_prefers_false_peak_label(self):
        s = 0.1
        birth = 1e-6
        false_mass = birth * math.exp(-gaussian_log_odds(s))
        x = candidate(s, birth, 1-birth-false_mass)
        result = decompose_candidate(x)
        self.assertAlmostEqual(result["inferred_false"], false_mass, places=10)
        self.assertLess(result["inferred_epsilon"], 1e-8)

    def test_high_strength_can_be_dominated_by_epsilon(self):
        x = decompose_candidate(candidate(0.95, 1e-9))
        self.assertGreater(x["inferred_epsilon"], 0.99)
        self.assertLess(x["inferred_false"], 1e-15)

    def test_existing_track_competes_even_for_weak_epsilon(self):
        s = 0.5
        birth = 0.004
        false_mass = birth * math.exp(-gaussian_log_odds(s))
        x = candidate(s, birth, 1-birth-false_mass-0.0005)
        result = decompose_candidate(x)
        self.assertAlmostEqual(result["inferred_epsilon"], 0.0005, places=7)

    def test_invalid_born_and_track_probability_mass_rejected(self):
        with self.assertRaisesRegex(AuditError, "not normalized"):
            decompose_candidate(candidate(0.4, 0.8, 0.5))

    def test_inconsistent_false_label_ratio_rejected(self):
        with self.assertRaisesRegex(AuditError, "exceeds residual"):
            decompose_candidate(candidate(0.1, 0.01, 0.5))

    def test_boolean_strength_rejected(self):
        with self.assertRaisesRegex(AuditError, "strength"):
            decompose_candidate(candidate(True, 0.1))

    def test_boolean_posterior_rejected(self):
        with self.assertRaisesRegex(AuditError, "birth posterior"):
            decompose_candidate(candidate(0.3, True))

    def test_invalid_existing_track_ID_rejected(self):
        x = candidate(0.3, 0.01)
        x["track_posteriors"] = [{"track_id": 7, "posterior": 0.1},
                                 {"track_id": 7, "posterior": 0.1}]
        with self.assertRaisesRegex(AuditError, "identity"):
            decompose_candidate(x)

    def test_nonfinite_probability_rejected(self):
        x = candidate(0.3, float("nan"))
        with self.assertRaisesRegex(AuditError, "birth posterior"):
            decompose_candidate(x)

    def test_empty_trace_fails_closed(self):
        with self.assertRaisesRegex(AuditError, "WAV/ODAS"):
            summarize_case([], 128)

    def test_noncontiguous_hop_rejected(self):
        x = record(0, [candidate(0.95, 1e-9)])
        x["sample_offset"] = 128
        with self.assertRaisesRegex(AuditError, "timeline"):
            summarize_case([x], 128)

    def test_actual_record_shape_and_epsilon_count(self):
        rows = [record(0, [candidate(0.95, 1e-9)]),
                record(1, [candidate(0.4, 0.6, 0.3)])]
        result = summarize_case(rows, 256)
        self.assertEqual(result["epsilon_dominant"], 1)
        self.assertEqual(result["strong_ssl_gt_0_8"], 1)
        self.assertEqual(result["strong_ssl_gt_0_8_birth_lt_0_01"], 1)
        self.assertEqual(result["hops"], 2)

    def test_no_source_energy_is_still_measured_not_speech(self):
        rows = [record(0, [candidate(0.0, 1e-9)])]
        result = summarize_case(rows, 128)
        self.assertEqual(result["hops"], 1)
        self.assertEqual(result["candidates"], 1)

    def test_birth_threshold_strength_is_stricter_than_equal_odds(self):
        result = summarize_case([record(0)], 128)
        self.assertAlmostEqual(
            result["no_existing_tracks_birth_0_9_strength_lower_bound"],
            0.36162041, places=6)


if __name__ == "__main__":
    unittest.main()
