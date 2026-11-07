#!/usr/bin/env python3
"""No-dependency S1 sample-clock and source-activity negative tests."""
from __future__ import annotations

import copy
import tempfile
import unittest
from pathlib import Path
import wave

from acoustix_timing_contract import verify_timing

FS = 16000
FRAMES = 24000  # 1.5 seconds, partial 128-frame hop with midpoint at EOF.


def fixture(frames=FRAMES):
    scene = {
        "sample_rate_hz": FS,
        "duration_s": 2.0,
        "sources": [
            {"id": "a", "active_s": [[0.0, 1.5]]},
            {"id": "b", "active_s": [[0.5, 1.0]]},
        ],
    }
    output = ((frames + 3 * FS + 127) // 128) * 128
    hops = []
    for index in range((frames + 127) // 128):
        midpoint = index * 128 + 64
        t = midpoint / FS
        sources = [
            {"id": source["id"]}
            for source in scene["sources"]
            if midpoint < frames and any(
                start <= t < stop for start, stop in source["active_s"])
        ]
        hops.append({"hop": index, "t_s": round(t, 4), "sources": sources})
    return scene, {
        "sample_rate_hz": FS,
        "simulated_frames": frames,
        "wav_frames": output,
        "ground_truth": hops,
    }


class PcmClockTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="acoustix-clock-")
        self.addCleanup(temp.cleanup)
        self.wav = Path(temp.name) / "input.wav"
        self.scene, self.manifest = fixture()
        self.write_wav(self.manifest["wav_frames"])

    def write_wav(self, frames, sample_rate=FS, channels=6):
        with wave.open(str(self.wav), "wb") as writer:
            writer.setnchannels(channels)
            writer.setsampwidth(2)
            writer.setframerate(sample_rate)
            writer.writeframes(b"\x00" * (frames * channels * 2))

    def verify(self):
        return verify_timing(self.scene, self.manifest, self.wav)

    def test_partial_last_hop_and_padding_are_valid(self):
        evidence = self.verify()
        self.assertEqual(evidence["partial_last_hop_frames"], 64)
        self.assertEqual(self.manifest["ground_truth"][-1]["sources"], [])

    def test_full_hop_boundary(self):
        self.scene, self.manifest = fixture(128 * 187)
        self.write_wav(self.manifest["wav_frames"])
        self.assertEqual(self.verify()["partial_last_hop_frames"], 0)

    def test_missing_or_duplicate_hop_is_rejected(self):
        self.manifest["ground_truth"].pop(10)
        with self.assertRaisesRegex(ValueError, "number of geometric oracle hops"):
            self.verify()
        self.scene, self.manifest = fixture()
        self.manifest["ground_truth"][10]["hop"] = 9
        with self.assertRaisesRegex(ValueError, "hop ID"):
            self.verify()

    def test_timestamp_drift_is_rejected(self):
        self.manifest["ground_truth"][4]["t_s"] += 0.008
        with self.assertRaisesRegex(ValueError, "timestamp"):
            self.verify()

    def test_source_onset_or_stop_mismatch_is_rejected(self):
        self.manifest["ground_truth"][150]["sources"] = [{"id": "a"}, {"id": "b"}]
        with self.assertRaisesRegex(ValueError, "Active source disagreement"):
            self.verify()

    def test_partial_hop_cannot_claim_post_end_source(self):
        self.manifest["ground_truth"][-1]["sources"] = [{"id": "a"}]
        with self.assertRaisesRegex(ValueError, "Active source disagreement"):
            self.verify()

    def test_padding_and_wav_header_mismatches_are_rejected(self):
        self.manifest["wav_frames"] += 128
        with self.assertRaisesRegex(ValueError, "three seconds"):
            self.verify()
        self.scene, self.manifest = fixture()
        self.write_wav(self.manifest["wav_frames"] - 128)
        with self.assertRaisesRegex(ValueError, "WAV header"):
            self.verify()
        self.write_wav(self.manifest["wav_frames"], sample_rate=8000)
        with self.assertRaisesRegex(ValueError, "WAV header"):
            self.verify()

    def test_duplicate_active_source_rejected(self):
        self.manifest["ground_truth"][0]["sources"].append({"id": "a"})
        with self.assertRaisesRegex(ValueError, "Duplicated active"):
            self.verify()


if __name__ == "__main__":
    unittest.main()
