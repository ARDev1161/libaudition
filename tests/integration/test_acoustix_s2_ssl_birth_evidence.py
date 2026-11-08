"""Contract tests for geometry/birth-audit code, not acoustic evidence."""
import copy
import math
import unittest

from acoustix_s2_ssl_birth_evidence import (
    angle_deg, analyse, choose_two, distribution, streaks)


def speaker(idx, bearing):
    return {"id": f"speaker_{idx}", "kind": "voice",
            "bearing_deg": bearing, "elevation_deg": 0.0}


def cand(bearing, p=0.01, strength=0.2, associated=0.7):
    az = math.radians(bearing)
    return {"x": math.cos(az), "y": math.sin(az), "z": 0.0,
            "strength": strength, "birth_posterior": p,
            "track_posteriors": [{"track_id": 9, "posterior": associated}]}


def data():
    return [{
        "hop_index": 0, "odas_timestamp": 1, "sample_offset": 0,
        "theta_new": 0.9,
        "ssl_candidates": [cand(-30), cand(30)],
        "tracks_after": [{"state": "A"}, {"state": "P"}],
    }], [{"hop": 0, "sources": [speaker(1, -30), speaker(2, 30)]}]


class RealEvidenceContractTests(unittest.TestCase):
    def test_two_distinct_candidate_matches(self):
        rows, oracle = data()
        report = analyse(rows, oracle)
        self.assertEqual(report["two_distinct_ssl_direction_hops"], 1)
        self.assertEqual(report["two_active_sst_tracks_hops"], 0)
        self.assertEqual(report["any_two_candidates_above_birth_threshold_hops"], 0)

    def test_no_double_use_of_one_candidate(self):
        people = [speaker(1, -5), speaker(2, 5)]
        self.assertIsNone(choose_two(people, [cand(0)], 25))

    def test_angular_metric_is_three_dimensional(self):
        people = [speaker(1, -30), speaker(2, 30)]
        a, b = cand(-30), cand(30)
        a.update({"x": 0.5 * a["x"], "y": 0.5 * a["y"],
                  "z": math.sqrt(0.75)})
        self.assertIsNone(choose_two(people, [a, b], 25))

    def test_select_nearest_not_strongest_candidate(self):
        people = [speaker(1, -30), speaker(2, 30)]
        choices = [cand(-15, strength=1), cand(-30, strength=0.02), cand(30)]
        self.assertEqual(choose_two(people, choices, 25), (1, 2))

    def test_zero_direction_ignored(self):
        people = [speaker(1, -30), speaker(2, 30)]
        blank = cand(0)
        blank.update({"x": 0.0, "y": 0.0, "z": 0.0})
        self.assertIsNone(choose_two(people, [blank, cand(30)], 45))

    def test_nonunit_candidate_rejected(self):
        people = [speaker(1, -30), speaker(2, 30)]
        bad = cand(-30)
        bad["x"] = 2
        with self.assertRaisesRegex(ValueError, "nonunit"):
            choose_two(people, [bad, cand(30)], 25)

    def test_nonfinite_candidate_rejected(self):
        people = [speaker(1, -30), speaker(2, 30)]
        bad = cand(-30)
        bad["x"] = float("nan")
        with self.assertRaisesRegex(ValueError, "nonfinite"):
            choose_two(people, [bad, cand(30)], 25)

    def test_two_candidate_birth_thresholds(self):
        rows, oracle = data()
        rows[0]["ssl_candidates"][0]["birth_posterior"] = 0.95
        rows[0]["ssl_candidates"][1]["birth_posterior"] = 0.97
        # Keep the independent mixture-label posterior simplex valid.
        for candidate in rows[0]["ssl_candidates"]:
            candidate["track_posteriors"][0]["posterior"] = 0.01
        r = analyse(rows, oracle)
        self.assertEqual(r["any_two_candidates_above_birth_threshold_hops"], 1)
        self.assertEqual(r["matched_pair_both_above_birth_threshold_hops"], 1)

    def test_unassigned_false_peak_mass_dominates(self):
        rows, oracle = data()
        for c in rows[0]["ssl_candidates"]:
            c["birth_posterior"] = 0.001
            c["track_posteriors"][0]["posterior"] = 0.02
        result = analyse(rows, oracle)
        for target in result["speaker_direction_matches"].values():
            self.assertEqual(target["dominant_posterior_class"]["unassigned_residual"], 1)
            self.assertGreater(target["unassigned_or_false_peak_mass"]["median"], 0.97)

    def test_existing_track_mass_dominates(self):
        rows, oracle = data()
        for c in rows[0]["ssl_candidates"]:
            c["birth_posterior"] = 0.01
            c["track_posteriors"][0]["posterior"] = 0.95
        result = analyse(rows, oracle)
        for target in result["speaker_direction_matches"].values():
            self.assertEqual(target["dominant_posterior_class"]["existing_track"], 1)

    def test_invalid_probability_simplex_rejected(self):
        rows, oracle = data()
        rows[0]["ssl_candidates"][0]["birth_posterior"] = 0.7
        rows[0]["ssl_candidates"][0]["track_posteriors"][0]["posterior"] = 0.7
        with self.assertRaisesRegex(ValueError, "exceed unit probability"):
            analyse(rows, oracle)

    def test_two_distinct_proposals_do_not_imply_two_active_tracks(self):
        rows, oracle = data()
        rows[0]["tracks_after"] = [{"state": "A"}]
        r = analyse(rows, oracle)
        self.assertEqual(r["two_distinct_ssl_direction_hops"], 1)
        self.assertEqual(r["two_active_sst_tracks_hops"], 0)

    def test_broken_odas_offset_rejected(self):
        rows, oracle = data()
        rows[0]["sample_offset"] = 128
        with self.assertRaisesRegex(ValueError, "timestamp or sample offset"):
            analyse(rows, oracle)

    def test_broken_oracle_hop_rejected(self):
        rows, oracle = data()
        oracle[0]["hop"] = 5
        with self.assertRaisesRegex(ValueError, "oracle timeline"):
            analyse(rows, oracle)

    def test_no_two_voice_overlap_is_not_success(self):
        rows, oracle = data()
        oracle[0]["sources"] = [speaker(1, -30)]
        with self.assertRaisesRegex(ValueError, "no actual two-voice overlap"):
            analyse(rows, oracle)

    def test_nonfinite_birth_rejected(self):
        rows, oracle = data()
        rows[0]["ssl_candidates"][0]["birth_posterior"] = float("inf")
        with self.assertRaisesRegex(ValueError, "invalid birth posterior"):
            analyse(rows, oracle)

    def test_invalid_track_posterior_rejected(self):
        rows, oracle = data()
        rows[0]["ssl_candidates"][0]["track_posteriors"][0]["posterior"] = True
        with self.assertRaisesRegex(ValueError, "invalid track association"):
            analyse(rows, oracle)

    def test_temporal_run_lengths(self):
        self.assertEqual(streaks([True, False, True, True, True, False, True])["runs"], 3)
        self.assertEqual(streaks([True, False, True, True, True, False, True])["longest_hops"], 3)
        self.assertEqual(streaks([True]*5)["runs_at_least_5_hops"], 1)

    def test_distribution_without_values(self):
        self.assertIsNone(distribution([]))
        self.assertEqual(distribution([0, 1])["median"], 0.5)


if __name__ == "__main__":
    unittest.main()
