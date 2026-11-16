#!/usr/bin/env python3
"""Deterministic contract tests for the experimental four-mic MUSIC probe."""
import importlib.util
import tempfile
import unittest
import wave
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("probe", HERE / "acoustix_normmusic_probe.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def delayed_broadband(degrees, seed=9, count=8192, rate=16000):
    rng = np.random.default_rng(seed)
    n = count + 4096
    freq = np.fft.rfftfreq(n, 1 / rate)
    base = np.fft.rfft(rng.standard_normal(n))
    base[(freq < 600) | (freq > 3000)] = 0
    direction = np.array([np.cos(np.deg2rad(degrees)), np.sin(np.deg2rad(degrees))])
    delays = probe.MIC_XY @ direction / probe.C
    channels = [np.fft.irfft(base * np.exp(-2j * np.pi * freq * tau), n=n)
                for tau in delays]
    audio = np.stack(channels, axis=1)[2048:2048 + count]
    return audio / max(np.max(abs(audio)), 1e-9) * 0.5


def circular_error(a, b):
    return abs((a - b + 180) % 360 - 180)


class NormMusicTests(unittest.TestCase):
    def test_azimuth_sign_and_quadrants(self):
        for direction in (-120, -45, 0, 40, 90, 155):
            with self.subTest(direction=direction):
                audio = delayed_broadband(direction)
                angles, score, used = probe.music_spectrum(audio, 16000)
                self.assertGreater(used, 0)
                self.assertLessEqual(circular_error(float(angles[np.argmax(score)]), direction), 8)

    def test_silence_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "no non-silent"):
            probe.music_spectrum(np.zeros((2048, 4)), 16000)

    def test_insufficient_frames_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "at least eight"):
            probe.music_spectrum(np.zeros((512, 4)), 16000)

    def test_invalid_source_hypothesis_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "sources must"):
            probe.music_spectrum(np.zeros((2048, 4)), 16000, sources=4)

    def test_acoustix_pcm_channel_mapping(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "six.wav"
            data = np.zeros((2048, 6), dtype="<i2")
            data[:, 0] = 100
            data[:, 1:5] = [1000, 2000, 3000, 4000]
            data[:, 5] = 500
            with wave.open(str(path), "wb") as out:
                out.setnchannels(6)
                out.setsampwidth(2)
                out.setframerate(16000)
                out.writeframes(data.tobytes())
            rate, actual = probe.read_acoustix(path)
            self.assertEqual(rate, 16000)
            np.testing.assert_allclose(actual[0], np.array([1000, 2000, 3000, 4000]) / 32768)


if __name__ == "__main__":
    unittest.main()
