#!/usr/bin/env python3
"""Pure-stdlib negative/positive S2 corpus contract tests.

These WAVs are algorithmically generated to test file validation ONLY.
They do not contain human speech and are never scored as real S2 speech.
"""
from __future__ import annotations

import copy
import hashlib
import math
from pathlib import Path
import struct
import tempfile
import unittest
import wave

from acoustix_speech_corpus import validate_corpus


def write_pcm(path: Path, frequency: int, *, fs: int = 16000,
              amplitude: int = 3000, frames: int = 9000) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    frames_list = (
        int(amplitude * math.sin(2 * math.pi * frequency * i / fs))
        for i in range(frames)
    )
    data = b"".join(struct.pack("<h", x) for x in frames_list)
    with wave.open(str(path), "wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(fs)
        writer.writeframes(data)


class SpeechCorpusContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="libaudition-s2-corpus-")
        cls.root = Path(cls.temp.name)
        speakers = []
        for index in range(1, 8):
            sid = "speaker_" + str(index)
            clips = []
            for number in range(3):
                relative = f"{sid}/utterance_{number}.wav"
                file = cls.root / relative
                write_pcm(file, 173 + index * 13 + number * 7)
                clips.append({
                    "utterance_id": f"test_{index}_{number}",
                    "path": relative,
                    "sha256": hashlib.sha256(file.read_bytes()).hexdigest(),
                    "split": "development" if number == 0 else "evaluation",
                    "transcript": "THIS IS ONLY NONSPEECH TEST DATA",
                })
            speakers.append({
                "source_id": sid,
                "speaker_id": "corpus_subject_" + str(index),
                "utterances": clips,
            })
        cls.manifest = {
            "schema": "libaudition.acoustix-speech-corpus.v1",
            "provenance": {
                "corpus_name": "SYNTHETIC TEST ONLY — NOT S2",
                "source_url": "https://www.openslr.org/12/",
                "license_id": "CC-BY-4.0",
                "license_url": "https://creativecommons.org/licenses/by/4.0/",
                "language": "en",
            },
            "speakers": speakers,
        }

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def verify(self, doc=None):
        return validate_corpus(doc or self.manifest, self.root)

    def test_valid_structure_and_pcm_checks(self):
        report = self.verify()
        self.assertEqual(report["distinct_speaker_count"], 7)
        self.assertEqual(report["distinct_utterance_count"], 21)
        self.assertEqual(report["development_count"], 7)
        self.assertEqual(report["evaluation_count"], 14)
        self.assertTrue(report["pcm_checked"])
        self.assertTrue(all(row["pcm"]["duration_s"] > 0.5
                            for row in report["utterances"]))
        self.assertIn("human speech authenticity and read-aloud transcript correctness",
                      report["not_validated"])

    def test_seven_unique_speakers_enforced(self):
        data = copy.deepcopy(self.manifest)
        data["speakers"][1]["speaker_id"] = data["speakers"][0]["speaker_id"]
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.verify(data)
        data = copy.deepcopy(self.manifest)
        data["speakers"].pop()
        with self.assertRaisesRegex(ValueError, "seven distinct"):
            self.verify(data)

    def test_hard_holdout_per_speaker(self):
        data = copy.deepcopy(self.manifest)
        for clip in data["speakers"][0]["utterances"]:
            clip["split"] = "development"
        with self.assertRaisesRegex(ValueError, "evaluation"):
            self.verify(data)

    def test_duplicated_files_and_hashes(self):
        data = copy.deepcopy(self.manifest)
        data["speakers"][1]["utterances"][2]["sha256"] = (
            data["speakers"][0]["utterances"][0]["sha256"])
        with self.assertRaisesRegex(ValueError, "Reused"):
            self.verify(data)

    def test_rejects_path_traversal(self):
        for malicious in ("../outside.wav", "/etc/passwd", "speaker_1/../../outside.wav",
                          "speaker_1\\outside.wav", "speaker_1/x.mp3"):
            with self.subTest(path=malicious):
                data = copy.deepcopy(self.manifest)
                data["speakers"][0]["utterances"][0]["path"] = malicious
                with self.assertRaisesRegex(ValueError, "Unsafe|escapes"):
                    self.verify(data)

    def test_missing_audio_mismatch_and_missing_transcript(self):
        data = copy.deepcopy(self.manifest)
        data["speakers"][0]["utterances"][0]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.verify(data)
        data = copy.deepcopy(self.manifest)
        data["speakers"][0]["utterances"][0]["transcript"] = ""
        with self.assertRaisesRegex(ValueError, "transcript"):
            self.verify(data)

    def test_wrong_format_and_silence_fail(self):
        path = self.root / self.manifest["speakers"][0]["utterances"][0]["path"]
        before = path.read_bytes()
        try:
            write_pcm(path, 330, fs=8000)
            data = copy.deepcopy(self.manifest)
            data["speakers"][0]["utterances"][0]["sha256"] = (
                hashlib.sha256(path.read_bytes()).hexdigest())
            with self.assertRaisesRegex(ValueError, "mono PCM16 16 kHz"):
                self.verify(data)
            write_pcm(path, 330, amplitude=0)
            data["speakers"][0]["utterances"][0]["sha256"] = (
                hashlib.sha256(path.read_bytes()).hexdigest())
            with self.assertRaisesRegex(ValueError, "silent|sparse"):
                self.verify(data)
        finally:
            path.write_bytes(before)

    def test_bad_license_is_rejected(self):
        data = copy.deepcopy(self.manifest)
        data["provenance"]["license_id"] = "NOASSERTION"
        with self.assertRaisesRegex(ValueError, "license"):
            self.verify(data)


if __name__ == "__main__":
    unittest.main()
