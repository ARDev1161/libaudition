#!/usr/bin/env python3
"""Stdlib-only Acoustix-to-ODAS quality diagnostic regression tests."""
from __future__ import annotations

import math
import unittest

from acoustix_metrics import angular_error_degrees, compute_metrics, match_sources_to_tracks


def voice(name, az, elevation=0):
    return {"id": name, "kind": "voice",
            "bearing_deg": float(az), "elevation_deg": float(elevation)}


def track(name, az, elevation=0):
    a, e = math.radians(az), math.radians(elevation)
    return {"id": name, "kind": "track", "hop": "0",
            "x": str(math.cos(e) * math.cos(a)),
            "y": str(math.cos(e) * math.sin(a)),
            "z": str(math.sin(e))}


SUMMARY = {"track_observations": 2, "potential_observations": 5,
           "separated_frames": 2, "recordings": 1, "recorder_drops": 0}


class AcoustixMetricsTests(unittest.TestCase):
    def test_three_dimensional_direction(self):
        self.assertAlmostEqual(angular_error_degrees(voice("a", 45, 30),
                                                     track(1, 45, 30)), 0, places=5)
        self.assertGreater(angular_error_degrees(voice("a", 45, 30),
                                                 track(1, 45, 0)), 25)

    def test_max_cardinality_is_not_a_greedy_nearest_match(self):
        # Track 0 is nearest to A, but only track 0 can serve B.
        # The optimal cardinality assignment is A<-track1, B<-track0.
        sources = [voice("A", 0), voice("B", 40)]
        tracks = [track("shared", 19), track("A-only", -19)]
        assignment = match_sources_to_tracks(sources, tracks)
        self.assertEqual(len(assignment), 2)
        self.assertEqual({s["id"]: t["id"] for s, t, _ in assignment},
                         {"A": "A-only", "B": "shared"})

    def test_unique_track_cannot_satisfy_seven_speakers(self):
        sources = [voice(f"speaker_{i}", i * 4) for i in range(7)]
        assignment = match_sources_to_tracks(sources, [track("one", 12)])
        self.assertEqual(len(assignment), 1)

    def test_outside_tolerance_is_not_detected(self):
        self.assertEqual(match_sources_to_tracks([voice("A", 0)],
                                                 [track("other", 90)]), [])

    def test_adjacent_matched_id_switch_and_coverage(self):
        truth = [
            {"hop": 0, "sources": [voice("A", 0), voice("B", 90)]},
            {"hop": 1, "sources": [voice("A", 0), voice("B", 90)]},
            {"hop": 2, "sources": [voice("A", 0), voice("B", 90)]},
        ]
        t0 = track("101", 0)
        t1 = track("102", 0)
        b0 = track("202", 90)
        observed = [dict(t0, hop="0"), dict(b0, hop="0"),
                    dict(t0, hop="1"), dict(b0, hop="1"),
                    dict(t1, hop="2")]
        metrics = compute_metrics(truth, observed, SUMMARY)
        self.assertEqual(metrics["active_voice_source_hops"], 6)
        self.assertEqual(metrics["matched_voice_source_hops"], 5)
        self.assertEqual(metrics["max_simultaneous_tracks"], 2)
        self.assertEqual(metrics["adjacent_matched_id_switches"], 1)
        self.assertEqual(metrics["adjacent_matched_id_comparisons"], 3)
        self.assertAlmostEqual(metrics["matched_voice_source_fraction"], 5/6, places=5)

    def test_gapped_track_does_not_count_as_adjacent_switch(self):
        truth = [{"hop": i, "sources": [voice("A", 0)]} for i in range(3)]
        observed = [dict(track("1", 0), hop="0"), dict(track("2", 0), hop="2")]
        metrics = compute_metrics(truth, observed, SUMMARY)
        self.assertEqual(metrics["adjacent_matched_id_switches"], 0)
        self.assertEqual(metrics["adjacent_matched_id_comparisons"], 0)

    def test_tracking_after_last_nonzero_mic_sample(self):
        truth = [{"hop": 0, "sources": [voice("A", 0)]}]
        summary = dict(SUMMARY, sample_rate_hz=16000, input_frames=1280)
        observed = [dict(track("active", 0), hop="0"),
                    dict(track("linger", 0), hop="4"),
                    dict(track("linger", 0), hop="5"),
                    dict(track("linger", 0), hop="7")]
        report = compute_metrics(truth, observed, summary,
                                 silence_start_frame=384)
        self.assertEqual(report["microphone_digital_silence_start_s"], .024)
        self.assertEqual(report["microphone_digital_silence_track_observations"], 3)
        self.assertEqual(report["microphone_digital_silence_tracked_hops"], 3)
        self.assertEqual(report["microphone_digital_silence_distinct_track_ids"], 1)
        self.assertEqual(report["microphone_digital_silence_available_hops"], 7)
        self.assertEqual(report["microphone_digital_silence_track_last_seen_s"], .056)

    def test_no_active_speech_is_reported_not_faked(self):
        truth = [{"hop": 0, "sources": []}]
        metrics = compute_metrics(truth, [], SUMMARY)
        self.assertEqual(metrics["active_voice_hops"], 0)
        self.assertIsNone(metrics["matched_voice_source_fraction"])
        self.assertIsNone(metrics["matched_median_3d_angular_error_deg"])


if __name__ == "__main__":
    unittest.main()
