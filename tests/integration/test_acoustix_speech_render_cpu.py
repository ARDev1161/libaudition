#!/usr/bin/env python3
"""Real Acoustix CPU integration of S2 sample placement (non-speech test PCM).

This uses actual CPU image-source RIRs, but the input is a TEST TONE and
must never be labelled human speech, WER or separation qualification.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
import wave

from acoustix_scene import render
from acoustix_timing_contract import verify_timing
from test_acoustix_replay import encoded_scene, make_scene
from test_acoustix_speech_corpus import SpeechCorpusContractTests


class RealCpuPlacementTests(unittest.TestCase):
    def test_start_preservation_with_real_rir(self):
        SpeechCorpusContractTests.setUpClass()
        try:
            corpus = SpeechCorpusContractTests.manifest
            root = SpeechCorpusContractTests.root
            scene = make_scene("static_one_voice")
            with tempfile.TemporaryDirectory(prefix="s2-placement-cpu-") as folder:
                out = Path(folder)
                scene_path = out / "scene.json"
                scene_path.write_bytes(encoded_scene(scene))
                corpus_path = out / "corpus.json"
                corpus_path.write_text(json.dumps(corpus, indent=2) + "\n")
                corpus_sha = hashlib.sha256(corpus_path.read_bytes()).hexdigest()
                plan_path = out / "plan.json"
                plan_path.write_text(json.dumps({
                    "schema": "libaudition.acoustix-speech-plan.v1",
                    "corpus_manifest_sha256": corpus_sha,
                    "split": "development",
                    "segments": [{
                        "source_id": "speaker_1",
                        "interval_index": 0,
                        "utterance_id": "test_1_0",
                    }],
                }, indent=2) + "\n")
                rendered = out / "render"
                render(scene, SimpleNamespace(
                    scene=scene_path, output=rendered,
                    seed=1161, hop_ms=250,
                    max_seconds=None, speech_dir=None,
                    speech_corpus_manifest=corpus_path,
                    speech_corpus_root=root,
                    speech_plan=plan_path, background_snr_db=None,
                    stems=True, runner=None, compare_capacity=False,
                    uncalibrated_rt60=False))
                gt = json.loads((rendered / "ground_truth.json").read_text())
                schedule = json.loads((rendered / "speech_schedule.json").read_text())
                self.assertEqual(gt["speech_schedule"], schedule)
                self.assertEqual(schedule["speech_event_count"], 1)
                self.assertEqual(schedule["segments"][0]["scheduled_start_frame"], 3200)
                self.assertEqual(schedule["segments"][0]["original_utterance_frames"], 9000)
                self.assertEqual(verify_timing(scene, gt, rendered / "input.wav")["sample_rate_hz"],
                                 16000)
                with wave.open(str(rendered / "clean_refs" / "speaker_1.wav")) as reference:
                    pcm = reference.readframes(reference.getnframes())
                    self.assertEqual(reference.getnframes(), 24000)
                with wave.open(str(root / "speaker_1" / "utterance_0.wav")) as original:
                    utterance = original.readframes(original.getnframes())
                self.assertEqual(pcm[:3200 * 2], b"\x00" * (3200 * 2))
                self.assertEqual(pcm[3200 * 2:3200 * 2 + len(utterance)], utterance)
                self.assertEqual(
                    hashlib.sha256((rendered / "clean_refs" / "speaker_1.wav")
                                   .read_bytes()).hexdigest(),
                    schedule["clean_reference_wav_sha256"]["speaker_1"])
        finally:
            SpeechCorpusContractTests.tearDownClass()


if __name__ == "__main__":
    unittest.main()
