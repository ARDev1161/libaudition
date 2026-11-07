#!/usr/bin/env python3
"""Schema/decision contract fixtures only; NOT actual Acoustix measurements."""
import copy
import math
import unittest

from acoustix_s2_probation_audit import (
    AuditError, _two_distinct_ssl_candidates, probation_decisions)


def trial(values, active):
    """One synthetic provisional birth and five recorded posterior updates."""
    rows = []
    for hop in range(6):
        record = {
            "schema": "odas_sst_raw_v1",
            "hop_index": hop, "sample_offset": hop * 128,
            "births": [7] if hop == 0 else [],
            "retirements": [7] if hop == 5 and not active else [],
            "ssl_candidates": [{
                "index": 0, "x": 1.0, "y": 0.0, "z": 0.0,
                "track_posteriors": (
                    [] if hop == 0 else
                    [{"track_id": 7, "posterior": values[hop-1] }]),
            }],
            "tracks_after": (
                [{"track_id": 7, "state": "A" if hop == 5 else "P"}]
                if active or hop < 5 else []),
        }
        rows.append(record)
    return rows


class ProbationAuditTests(unittest.TestCase):
    def test_successful_five_hop_confirmation(self):
        events = probation_decisions(trial([0.95]*5, True), 5, 0.8)
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["decision"], "ACTIVE")
        self.assertEqual(events[0]["predicted"], "ACTIVE")
        self.assertAlmostEqual(events[0]["probation_mean"], 0.95)

    def test_rejection_below_threshold(self):
        events = probation_decisions(trial([0.6]*5, False), 5, 0.8)
        self.assertEqual(events[0]["decision"], "RETIRED")
        self.assertEqual(events[0]["predicted"], "RETIRED")

    def test_inconsistent_promotion_detected(self):
        with self.assertRaisesRegex(AuditError, "contradicts"):
            probation_decisions(trial([0.1]*5, True), 5, 0.8)

    def test_inconsistent_retirement_detected(self):
        with self.assertRaisesRegex(AuditError, "contradicts"):
            probation_decisions(trial([0.99]*5, False), 5, 0.8)

    def test_sum_is_union_not_maximum_candidate_probability(self):
        rows = trial([0.0]*5, True)
        for row in rows[1:]:
            row["ssl_candidates"] = [
                {"track_posteriors": [{"track_id": 7, "posterior": 0.6}]},
                {"track_posteriors": [{"track_id": 7, "posterior": 0.6}]},
            ]
        # Union = 1-(0.4^2) = 0.84, so this truly promotes.
        events = probation_decisions(rows, 5, 0.8)
        self.assertAlmostEqual(events[0]["probation_mean"], 0.84)

    def test_duplicate_birth_rejected(self):
        rows = trial([0.95]*5, True)
        rows[1]["births"] = [7]
        with self.assertRaisesRegex(AuditError, "duplicate birth"):
            probation_decisions(rows, 5, 0.8)

    def test_missing_prior_track_rejected(self):
        rows = trial([0.95]*5, True)
        rows[2]["ssl_candidates"][0]["track_posteriors"] = []
        with self.assertRaisesRegex(AuditError, "missing or duplicated"):
            probation_decisions(rows, 5, 0.8)

    def test_non_contiguous_hop_rejected(self):
        rows = trial([0.95]*5, True)
        rows[3]["sample_offset"] = 256
        with self.assertRaisesRegex(AuditError, "noncontiguous"):
            probation_decisions(rows, 5, 0.8)

    def test_boolean_posterior_rejected(self):
        rows = trial([0.95]*5, True)
        rows[1]["ssl_candidates"][0]["track_posteriors"][0]["posterior"] = True
        with self.assertRaisesRegex(AuditError, "invalid prior"):
            probation_decisions(rows, 5, 0.8)

    def test_missing_final_window_is_incomplete_not_pass(self):
        rows = trial([0.95]*5, True)[:4]
        events = probation_decisions(rows, 5, 0.8)
        self.assertEqual(events[0]["decision"], "INCOMPLETE")

    def test_invalid_parameters(self):
        with self.assertRaisesRegex(AuditError, "configuration"):
            probation_decisions(trial([0.95]*5, True), 0, 0.8)
        with self.assertRaisesRegex(AuditError, "configuration"):
            probation_decisions(trial([0.95]*5, True), 5, 1.2)

    def test_geometric_coverage_requires_two_different_ssl_candidates(self):
        sources = [
            {"bearing_deg": 20.0, "elevation_deg": 0.0},
            {"bearing_deg": -20.0, "elevation_deg": 0.0},
        ]
        candidates = [
            {"x": math.cos(math.radians(20)), "y": math.sin(math.radians(20)), "z": 0.0},
            {"x": math.cos(math.radians(-20)), "y": math.sin(math.radians(-20)), "z": 0.0},
        ]
        self.assertTrue(_two_distinct_ssl_candidates(sources, candidates, 10.0))
        self.assertFalse(_two_distinct_ssl_candidates(sources, candidates[:1], 45.0))
        self.assertFalse(_two_distinct_ssl_candidates(sources, [
            {"x": -1, "y": 0, "z": 0}, {"x": 0, "y": 0, "z": 1},
        ], 25.0))

    def test_unexpected_provisional_state_at_decision_rejected(self):
        rows = trial([0.95]*5, True)
        rows[5]["tracks_after"][0]["state"] = "P"
        with self.assertRaisesRegex(AuditError, "neither or both"):
            probation_decisions(rows, 5, 0.8)


if __name__ == "__main__":
    unittest.main()
