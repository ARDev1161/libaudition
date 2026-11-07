#!/usr/bin/env python3
"""Pure-standard-library 1/2/7-speaker S2 scene generator contracts.

All input WAVs are mathematical tones, NOT genuine speech. Only
metadata, scene construction and clip-preserving plan rules are tested.
"""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import unittest

from acoustix_scene import check_scene
from acoustix_speech_placement import place_speech_intervals
from prepare_acoustix_s2_scene import (DEFAULT_SCENE, build_scene_and_plan)
from test_acoustix_speech_corpus import SpeechCorpusContractTests

SHA = "1" * 64


class ScenePlanBuilderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SpeechCorpusContractTests.setUpClass()
        cls.corpus = SpeechCorpusContractTests.manifest
        cls.root = SpeechCorpusContractTests.root
        cls.template = json.loads(DEFAULT_SCENE.read_text())

    @classmethod
    def tearDownClass(cls):
        SpeechCorpusContractTests.tearDownClass()

    def build(self, **kwargs):
        params = dict(
            corpus_sha=SHA,
            speakers=1,
            split="development",
            robot_motion="stationary",
            include_background=False,
            rt60_s=0.36,
        )
        params.update(kwargs)
        return build_scene_and_plan(
            self.template, self.corpus, self.root, **params)

    def test_single_stationary_source_exact_utterance_length(self):
        scene, plan, report = self.build()
        self.assertEqual(len(scene["sources"]), 1)
        self.assertEqual(scene["sources"][0]["id"], "speaker_1")
        self.assertEqual(check_scene(scene)["voice_sources"], 1)
        self.assertEqual(scene["robot"]["trajectory"][0]["position_m"],
                         scene["robot"]["trajectory"][-1]["position_m"])
        self.assertEqual(len(plan["segments"]), 1)
        self.assertEqual(report["speaker_count"], 1)
        signals, schedule = place_speech_intervals(
            scene, self.corpus, self.root, plan, corpus_sha256=SHA)
        self.assertEqual(schedule["speech_event_count"], 1)
        self.assertEqual(schedule["segments"][0]["original_utterance_frames"], 9000)
        self.assertTrue(any(signals["speaker_1"]))

    def test_two_moving_voices_with_background(self):
        scene, plan, report = self.build(
            speakers=2, robot_motion="moving",
            include_background=True, rt60_s=0.7,
            split="evaluation")
        check_scene(scene)
        self.assertEqual(report["speaker_count"], 2)
        self.assertEqual(report["requested_rt60_s"], 0.7)
        self.assertEqual(len(scene["sources"]), 5)
        self.assertEqual(len(plan["segments"]), 2)
        self.assertTrue(all(p["utterance_id"].endswith("_1")
                            for p in plan["segments"]))
        self.assertEqual(len(scene["robot"]["trajectory"]), 9)

    def test_seven_speakers_and_original_fixture_unchanged(self):
        original = copy.deepcopy(self.template)
        scene, plan, report = self.build(
            speakers=7, robot_motion="moving", split="evaluation",
            include_background=True)
        check_scene(scene)
        self.assertEqual(len(plan["segments"]), 7)
        self.assertEqual(check_scene(scene)["voice_sources"], 7)
        self.assertEqual(
            {s["id"] for s in scene["sources"] if s["kind"] != "voice"},
            {"fridge", "ac", "car_window"})
        self.assertEqual(self.template, original)
        self.assertEqual(report["status"], "CORPUS_PCM_VALIDATED_NOT_RENDERED")
        self.assertEqual(
            json.dumps(scene, sort_keys=True),
            json.dumps(self.build(
                speakers=7, robot_motion="moving", split="evaluation",
            include_background=True)[0],
                       sort_keys=True))

    def test_invalid_split_source_count_and_target_rt60_rejected(self):
        with self.assertRaisesRegex(ValueError, "1, 2, or 7"):
            self.build(speakers=3)
        with self.assertRaisesRegex(ValueError, "Unsupported S2 split"):
            self.build(split="test")
        with self.assertRaisesRegex(ValueError, "target RT60"):
            self.build(rt60_s=0.95)


if __name__ == "__main__":
    unittest.main()
