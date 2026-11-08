"""Synthetic geometry-contract cases, never Acoustix source-quality evidence."""
import math
from pathlib import Path
import tempfile
import unittest

from acoustix_s2_gmm_doa_audit import (
    GeometryError, measure_t03_oracle_pairs, read_native_tracks)


def oracle():
    return [{"hop": 0, "sources": [
        {"id": "speaker_1", "kind": "voice", "bearing_deg": -20.0,
         "elevation_deg": 0.0},
        {"id": "speaker_2", "kind": "voice", "bearing_deg": 20.0,
         "elevation_deg": 0.0},
    ]}]


def track(tid, azimuth):
    theta = math.radians(azimuth)
    return {"track_id": tid, "x": math.cos(theta),
            "y": math.sin(theta), "z": 0.0}


class DoaContractTests(unittest.TestCase):
    def test_two_independent_accurate_tracks(self):
        r = measure_t03_oracle_pairs(
            oracle(), {0: [track(1, -20), track(2, 20)]})
        self.assertEqual(r["two_distinct_track_to_voice_matches_within_25deg_hops"], 1)
        self.assertEqual(r["two_distinct_track_to_voice_matches_within_45deg_hops"], 1)
        self.assertEqual(r["two_active_but_no_distinct_two_voice_match_45deg_hops"], 0)

    def test_two_tracks_do_not_imply_two_speakers(self):
        # An echo/source mislocalization 80 degrees away must not count as
        # detection of the second human.
        r = measure_t03_oracle_pairs(
            oracle(), {0: [track(1, -20), track(2, 100)]})
        self.assertEqual(r["two_or_more_active_tracks_in_overlap_hops"], 1)
        self.assertEqual(r["two_distinct_track_to_voice_matches_within_45deg_hops"], 0)
        self.assertEqual(r["two_active_but_no_distinct_two_voice_match_45deg_hops"], 1)

    def test_single_track_cannot_match_both_sources(self):
        r = measure_t03_oracle_pairs(oracle(), {0: [track(1, 0)]})
        self.assertEqual(r["two_or_more_active_tracks_in_overlap_hops"], 0)
        self.assertEqual(r["two_distinct_track_to_voice_matches_within_45deg_hops"], 0)

    def test_track_arrival_order_invariant(self):
        a, b = track(1, -20), track(2, 20)
        one = measure_t03_oracle_pairs(oracle(), {0: [a, b]})
        two = measure_t03_oracle_pairs(oracle(), {0: [b, a]})
        self.assertEqual(one["two_distinct_track_to_voice_matches_within_25deg_hops"],
                         two["two_distinct_track_to_voice_matches_within_25deg_hops"])

    def test_3d_elevation_important(self):
        high = track(2, 20)
        high["x"] *= 0.1
        high["y"] *= 0.1
        high["z"] = math.sqrt(0.99)
        r = measure_t03_oracle_pairs(oracle(), {0: [track(1, -20), high]})
        self.assertEqual(r["two_distinct_track_to_voice_matches_within_45deg_hops"], 0)

    def test_reject_nonunit_direction(self):
        bad = track(2, 20)
        bad["x"] *= 2.0
        with self.assertRaisesRegex(GeometryError, "nonunit"):
            measure_t03_oracle_pairs(oracle(), {0: [track(1, -20), bad]})

    def test_reject_broken_oracle_clock(self):
        data = oracle()
        data[0]["hop"] = 1
        with self.assertRaisesRegex(GeometryError, "discontinuity"):
            measure_t03_oracle_pairs(data, {0: [track(1, -20), track(2, 20)]})

    def test_empty_truth_is_not_a_pass(self):
        with self.assertRaisesRegex(GeometryError, "missing"):
            measure_t03_oracle_pairs([], {})

    def test_read_actual_native_csv_and_fail_closed_on_duplicate(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "observations.csv"
            p.write_text(
                "hop,kind,id,x,y,z,activity\n"
                "0,track,2,1,0,0,0.5\n"
                "1,proposal,-1,1,0,0,0.8\n", encoding="utf-8")
            r = read_native_tracks(p)
            self.assertEqual(len(r[0]), 1)
            self.assertEqual(len(r[1]), 0)
            with p.open("a", encoding="utf-8") as out:
                out.write("0,track,2,1,0,0,0.5\n")
            with self.assertRaisesRegex(GeometryError, "duplicate"):
                read_native_tracks(p)

    def test_csv_rejects_invalid_clock(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "observations.csv"
            p.write_text(
                "hop,kind,id,x,y,z,activity\n"
                "1875,track,2,1,0,0,0.5\n", encoding="utf-8")
            with self.assertRaisesRegex(GeometryError, "outside PCM"):
                read_native_tracks(p)


if __name__ == "__main__":
    unittest.main()
