#!/usr/bin/env python3
"""S1 malicious/malformed scene boundary contracts (stdlib only).

Do not pass distorted microphone geometry to Acoustix and do not allow
source IDs to escape the intended WAV/stem output directory. This is a
validation smoke, not a physics or source-separation quality test.
"""
from __future__ import annotations

import copy
import json
import math
import unittest
from pathlib import Path

from acoustix_scene import check_scene

FIXTURE = (Path(__file__).resolve().parents[1] /
           "fixtures/acoustix/table_conversation_7x7.json")


class SceneBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.scene = json.loads(FIXTURE.read_text(encoding="utf-8"))
        check_scene(self.scene)

    def reject(self, note: str, transform):
        changed = copy.deepcopy(self.scene)
        transform(changed)
        with self.subTest(note=note), self.assertRaises(ValueError):
            check_scene(changed)

    def test_microphone_geometry_rejects_invalid_cases(self):
        self.reject("coincident mics", lambda s:
                    s["robot"]["microphones_m"].__setitem__(
                        1, list(s["robot"]["microphones_m"][0])))
        self.reject("NaN mic offset", lambda s:
                    s["robot"]["microphones_m"][0].__setitem__(0, math.nan))
        self.reject("infinite mic offset", lambda s:
                    s["robot"]["microphones_m"][0].__setitem__(0, math.inf))
        self.reject("nonnumeric mic offset", lambda s:
                    s["robot"]["microphones_m"][0].__setitem__(0, "not-a-number"))
        self.reject("mic outside room despite robot center inside", lambda s:
                    s["robot"]["microphones_m"][0].__setitem__(0, 12.0))

    def test_yaw_and_trajectory_are_valid(self):
        self.reject("NaN yaw", lambda s:
                    s["robot"]["trajectory"][0].__setitem__("yaw_deg", math.nan))
        self.reject("infinite yaw", lambda s:
                    s["robot"]["trajectory"][0].__setitem__("yaw_deg", math.inf))
        self.reject("negative source movement time", lambda s:
                    s["sources"][-1]["trajectory"][0].__setitem__("t_s", -1.0))
        self.reject("movement beyond scene", lambda s:
                    s["sources"][-1]["trajectory"][-1].__setitem__("t_s", 13.0))
        self.reject("unordered robot keyframes", lambda s:
                    s["robot"]["trajectory"][1].__setitem__("t_s", 0.0))

    def test_source_identity_and_activity_are_valid(self):
        self.reject("output path traversal", lambda s:
                    s["sources"][0].__setitem__("id", "../outside"))
        self.reject("output filename separator", lambda s:
                    s["sources"][0].__setitem__("id", "speaker/one"))
        self.reject("duplicate identity", lambda s:
                    s["sources"][1].__setitem__("id", "speaker_1"))
        self.reject("overlapping activity intervals", lambda s:
                    s["sources"][0].__setitem__(
                        "active_s", [[0.2, 1.2], [0.8, 1.5]]))
        self.reject("activity outside duration", lambda s:
                    s["sources"][0].__setitem__(
                        "active_s", [[0.2, 16.0]]))

    def test_frozen_positive_cases_still_validate(self):
        from test_acoustix_replay import make_scene
        self.assertEqual(check_scene(self.scene)["microphones"], 4)
        for case in ("static_one_voice", "moving_two_voices",
                     "larger_room_three_voices"):
            self.assertEqual(check_scene(make_scene(case))["microphones"], 4)


if __name__ == "__main__":
    unittest.main(verbosity=2)
