#!/usr/bin/env python3
"""S2 interval-aligned speech placement contracts (synthetic PCM only).

The samples here are mathematical tones, NOT real speech; this gate tests
sample-index preservation, provenance and anti-truncation rules exclusively.
"""
from __future__ import annotations

from array import array
import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
import wave

from acoustix_speech_placement import (
    place_speech_intervals, write_clean_references
)
from test_acoustix_speech_corpus import SpeechCorpusContractTests


CORPUS_SHA = "a" * 64


def scene(intervals):
    return {"sample_rate_hz": 16000, "duration_s": 2.0,
            "sources": [{"id": "speaker_1", "kind": "voice",
                         "active_s": intervals},
                        {"id": "fridge", "kind": "fridge",
                         "active_s": [[0, 2.0]]}]}


def plan(segments, split="development"):
    return {
        "schema": "libaudition.acoustix-speech-plan.v1",
        "corpus_manifest_sha256": CORPUS_SHA,
        "split": split,
        "segments": [
            {"source_id": "speaker_1", "interval_index": i, "utterance_id": uid}
            for i, uid in enumerate(segments)
        ],
    }


def pcm(path):
    with wave.open(str(path), "rb") as reader:
        return reader.readframes(reader.getnframes())


class PlacementContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SpeechCorpusContractTests.setUpClass()
        cls.root = SpeechCorpusContractTests.root
        cls.corpus = SpeechCorpusContractTests.manifest

    @classmethod
    def tearDownClass(cls):
        SpeechCorpusContractTests.tearDownClass()

    def call(self, sc, plan_doc):
        return place_speech_intervals(
            sc, self.corpus, self.root, plan_doc, corpus_sha256=CORPUS_SHA)

    def test_clip_starts_at_interval_not_at_zero_with_no_lost_head(self):
        placed, report = self.call(scene([[0.2, 1.0]]), plan(["test_1_0"]))
        original = pcm(self.root / "speaker_1/utterance_0.wav")
        dry = placed["speaker_1"]
        self.assertEqual(len(dry), 32000)
        self.assertTrue(all(x == 0 for x in dry[:3200]))
        first = array("h")
        first.frombytes(original)
        self.assertEqual(dry[3200:3200 + len(first)], first)
        self.assertTrue(all(x == 0 for x in dry[3200 + len(first):]))
        self.assertEqual(report["segments"][0]["scheduled_start_frame"], 3200)

        with tempfile.TemporaryDirectory(prefix="s2-clean-") as folder:
            digests = write_clean_references(Path(folder), placed)
            ref = Path(folder) / "speaker_1.wav"
            with wave.open(str(ref), "rb") as source:
                self.assertEqual(source.getnchannels(), 1)
                self.assertEqual(source.getframerate(), 16000)
                self.assertEqual(source.getnframes(), 32000)
                self.assertEqual(source.readframes(32000)[3200 * 2:
                    (3200 + len(first)) * 2], original)
            self.assertEqual(digests["speaker_1"],
                             hashlib.sha256(ref.read_bytes()).hexdigest())

    def test_multiple_utterances_per_speaker_are_not_silently_recycled(self):
        sc = scene([[0.2, 0.8], [1.0, 1.6]])
        placed, report = self.call(
            sc, plan(["test_1_1", "test_1_2"], split="evaluation"))
        self.assertEqual(report["speech_event_count"], 2)
        self.assertTrue(any(placed["speaker_1"][3201:]))
        self.assertEqual(placed["speaker_1"][14000], 0)
        with self.assertRaisesRegex(ValueError, "reused"):
            self.call(sc, plan(["test_1_1", "test_1_1"], split="evaluation"))

    def test_missing_or_wrong_split_and_hash_fail(self):
        with self.assertRaisesRegex(ValueError, "one S2 utterance"):
            self.call(scene([[0.2, 1.0]]), plan([]))
        with self.assertRaisesRegex(ValueError, "another corpus split"):
            self.call(scene([[0.2, 1.0]]), plan(["test_1_1"]))
        bad = plan(["test_1_0"])
        bad["corpus_manifest_sha256"] = "b" * 64
        with self.assertRaisesRegex(ValueError, "another corpus manifest"):
            self.call(scene([[0.2, 1.0]]), bad)

    def test_short_interval_rejects_clip_without_cutting_words(self):
        with self.assertRaisesRegex(ValueError, "no truncation"):
            self.call(scene([[0.2, 0.3]]), plan(["test_1_0"]))

    def test_mismatched_source_and_interval_rejected(self):
        bad = plan(["test_1_0"])
        bad["segments"][0]["source_id"] = "speaker_2"
        with self.assertRaisesRegex(ValueError, "Unknown"):
            self.call(scene([[0.2, 1.0]]), bad)
        bad = plan(["test_1_0"])
        bad["segments"][0]["interval_index"] = 1
        with self.assertRaisesRegex(ValueError, "Unknown"):
            self.call(scene([[0.2, 1.0]]), bad)


if __name__ == "__main__":
    unittest.main()
