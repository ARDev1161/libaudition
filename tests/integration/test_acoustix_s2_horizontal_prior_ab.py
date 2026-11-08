"""Synthetic contract tests only; not evidence of actual acoustic performance."""
import csv
import json
import math
from pathlib import Path
import tempfile
import unittest
import wave

from acoustix_s2_horizontal_prior_ab import (
    active_verticality, analyze_variant, assert_source_integrity,
    evaluate, SweepError,
)
from acoustix_s2_gmm_doa_audit import GeometryError


def make_tracks(path, vectors):
    with path.open("w", newline="", encoding="utf-8") as src:
        writer = csv.writer(src)
        writer.writerow(("hop", "kind", "id", "x", "y", "z", "activity"))
        for i, (x, y, z) in enumerate(vectors, 1):
            writer.writerow((0, "track", i, x, y, z, 0.9))


class HorizontalPriorContracts(unittest.TestCase):
    def test_elevation_measure_vertical_track(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "obs.csv"
            make_tracks(p, [(1.0, 0.0, 0.0), (0.0, 0.0, 1.0)])
            d = active_verticality(p)
            self.assertEqual(d["active_track_observations"], 2)
            self.assertEqual(d["active_track_observations_above_45deg_absolute_elevation"], 1)
            self.assertAlmostEqual(d["median_absolute_elevation_deg"], 45, places=4)

    def test_empty_track_trace_is_not_claimed_accuracy(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "obs.csv"
            make_tracks(p, [])
            d = active_verticality(p)
            self.assertEqual(d["active_track_observations"], 0)
            self.assertIsNone(d["median_absolute_elevation_deg"])

    def test_bad_nonunit_track_vector_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "obs.csv"
            make_tracks(p, [(0.1, 0.1, 0.1)])
            with self.assertRaisesRegex(SweepError, "nonunit"):
                active_verticality(p)

    def test_duplicate_native_track_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "obs.csv"
            p.write_text(
                "hop,kind,id,x,y,z,activity\n"
                "0,track,1,1,0,0,0.9\n"
                "0,track,1,0,1,0,0.7\n", encoding="utf-8")
            with self.assertRaisesRegex(GeometryError, "duplicate"):
                active_verticality(p)

    def test_zero_real_wav_hash_is_not_accepted_as_real_voice(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "t03_eval_two/render").mkdir(parents=True)
            wav = p / "t03_eval_two/render/input.wav"
            with wave.open(str(wav), "wb") as dest:
                dest.setnchannels(6); dest.setsampwidth(2)
                dest.setframerate(16000)
                dest.writeframes(b"\0" * (240000*6*2))
            (p / "t03_eval_two/input_sha256.txt").write_text("not-a-valid-sha")
            with self.assertRaisesRegex(SweepError, "changed"):
                assert_source_integrity(p, "t03_eval_two")

    def test_cannot_reuse_previous_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "existing").mkdir()
            (p / "runner").write_text("dummy")
            (p / "validator").write_text("dummy")
            with self.assertRaisesRegex(SweepError, "Refusing overwrite"):
                evaluate(p, p/"runner", p/"validator", p/"existing")

    def test_track_oracle_time_mismatch_fails_closed(self):
        oracle = [{"hop": 0, "sources": [
            {"id": "human1", "kind": "voice", "bearing_deg": 0.0},
            {"id": "human2", "kind": "voice", "bearing_deg": 90.0},
        ]}]
        metrics = {"oracle_exactly_two_voice_hops": 1,
                   "oracle_two_voice_hops_with_at_least_two_active_tracks": 1}
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "obs.csv"
            make_tracks(p, [])   # no real active tracks despite claimed 2
            with self.assertRaisesRegex(SweepError, "disagree"):
                analyze_variant({}, {}, True, "t03_eval_two",
                                metrics, p, "sha", "trace-sha", oracle)

    def test_reported_track_identity_requires_independent_match(self):
        oracle = [{"hop": 0, "sources": [
            {"id": "human1", "kind": "voice", "bearing_deg": 0.0},
            {"id": "human2", "kind": "voice", "bearing_deg": 20.0},
        ]}]
        metrics = {"oracle_exactly_two_voice_hops": 1,
                   "oracle_two_voice_hops_with_at_least_two_active_tracks": 2}
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "obs.csv"
            make_tracks(p, [(1.0, 0.0, 0.0), (0.0, 0.0, 1.0)])
            with self.assertRaisesRegex(SweepError, "disagree"):
                analyze_variant({}, {}, True, "t03_eval_two",
                                metrics, p, "sha", "trace-sha", oracle)


if __name__ == "__main__":
    unittest.main()
