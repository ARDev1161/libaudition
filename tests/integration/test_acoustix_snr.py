#!/usr/bin/env python3
"""No-Acoustix background SNR control tests, including negative contracts."""
import math
import unittest

from acoustix_snr import background_snr_gain


class BackgroundSnrContracts(unittest.TestCase):
    def test_requested_targets_and_common_gain(self):
        voice = [0.1, -0.1, 0.2, -0.2] * 10
        background = [0.02, 0.02, -0.04, -0.04] * 10
        mask = [True, False, True, False] * 10
        for requested in (0.0, 6.0, 12.0, -5.0):
            gain, evidence = background_snr_gain(voice, background, mask, requested)
            self.assertAlmostEqual(evidence["measured_voice_to_background_snr_db"],
                                   requested, places=9)
            selected = [i for i, yes in enumerate(mask) if yes]
            signal_energy = sum(voice[i] ** 2 for i in selected)
            noise_energy = sum((background[i] * gain) ** 2 for i in selected)
            self.assertAlmostEqual(
                10 * math.log10(signal_energy / noise_energy), requested,
                places=9)

    def test_speech_and_background_must_both_be_audible(self):
        with self.assertRaisesRegex(ValueError, "non-silent"):
            background_snr_gain([0.0] * 4, [1.0] * 4, [True] * 4, 6)
        with self.assertRaisesRegex(ValueError, "non-silent"):
            background_snr_gain([1.0] * 4, [0.0] * 4, [True] * 4, 6)

    def test_invalid_shapes_nonfinite_and_mask_rejected(self):
        for requested in (float("nan"), float("inf"), -31, 61, True):
            with self.assertRaisesRegex(ValueError, "Requested background SNR"):
                background_snr_gain([1.0], [1.0], [True], requested)
        with self.assertRaisesRegex(ValueError, "nonzero lengths"):
            background_snr_gain([1.0, 2.0], [1.0], [True], 6)
        with self.assertRaisesRegex(ValueError, "booleans"):
            background_snr_gain([1.0], [1.0], [1], 6)
        with self.assertRaisesRegex(ValueError, "active speech"):
            background_snr_gain([1.0], [1.0], [False], 6)
        with self.assertRaisesRegex(ValueError, "finite"):
            background_snr_gain([float("nan")], [1.0], [True], 6)

    def test_inactive_samples_excluded_from_gain_reference(self):
        voice, background, active = [1.0, 0.0], [0.1, 10.0], [True, False]
        gain, report = background_snr_gain(voice, background, active, 0.0)
        self.assertAlmostEqual(gain, 10.0)
        self.assertEqual(report["active_sample_count"], 1)
        self.assertIn("SI-SDR", report["excludes"])


if __name__ == "__main__":
    unittest.main()
