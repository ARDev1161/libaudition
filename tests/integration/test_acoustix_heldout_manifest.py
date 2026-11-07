#!/usr/bin/env python3
"""Fast independent checks for S1 held-out acoustic configuration freeze."""
import copy
import hashlib
import unittest

from acoustix_heldout_manifest import (
    ROOM_RECIPES, RT60_TARGETS, SEEDS, entries, render_manifest, verify_manifest
)


class FrozenHeldoutManifestTests(unittest.TestCase):
    def test_repeated_generation_is_deterministic(self):
        self.assertEqual(render_manifest(), render_manifest())
        self.assertEqual(len(entries()), 20)
        self.assertEqual(len(SEEDS), 20)
        self.assertTrue(all(seed not in (1161, 2161, 3161)
                            for seed in SEEDS))
        self.assertEqual(set(ROOM_RECIPES),
                         {item["recipe"] for item in entries()})
        self.assertEqual(set(RT60_TARGETS),
                         {item["requested_rt60_s"] for item in entries()})
        self.assertEqual(len({item["scene_sha256"] for item in entries()}), 9)
        self.assertTrue(all(len(item["scene_sha256"]) == 64
                            for item in entries()))

    def test_duplicate_seed_is_rejected(self):
        cases = copy.deepcopy(entries())
        cases[5]["seed"] = cases[4]["seed"]
        with self.assertRaisesRegex(ValueError, "seeds or ordering"):
            verify_manifest(cases)

    def test_missing_room_or_rt60_is_rejected(self):
        cases = copy.deepcopy(entries())
        for item in cases:
            item["recipe"] = "static_one_voice"
        with self.assertRaisesRegex(ValueError, "room recipes"):
            verify_manifest(cases)
        cases = copy.deepcopy(entries())
        for item in cases:
            item["requested_rt60_s"] = 0.36
        with self.assertRaisesRegex(ValueError, "RT60"):
            verify_manifest(cases)

    def test_changing_clock_or_identity_is_rejected(self):
        cases = copy.deepcopy(entries())
        cases[0]["duration_s"] = 1.0
        with self.assertRaisesRegex(ValueError, "timebase"):
            verify_manifest(cases)
        cases = copy.deepcopy(entries())
        cases[0]["case_id"] = cases[1]["case_id"]
        with self.assertRaisesRegex(ValueError, "scenario identity"):
            verify_manifest(cases)

    def test_metadata_never_claims_real_speech_or_rendered_pass(self):
        serialized = render_manifest()
        self.assertIn("CONFIGURATION_ONLY_NOT_RENDERED", serialized)
        self.assertIn("Synthetic voiced excitations", serialized)
        self.assertIn("No target/interferer SNR fixture yet", serialized)


if __name__ == "__main__":
    unittest.main()
