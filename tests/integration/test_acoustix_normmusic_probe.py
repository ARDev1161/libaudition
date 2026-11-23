#!/usr/bin/env python3
"""Deterministic contract tests for the experimental four-mic MUSIC probe."""
import importlib.util
import json
import subprocess
import sys
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
    def test_geometry_matches_frozen_acoustix_scene(self):
        scene = HERE.parent / "fixtures" / "acoustix" / "table_conversation_7x7.json"
        fixture = json.loads(scene.read_text(encoding="utf-8"))
        expected = np.asarray(fixture["robot"]["microphones_m"], dtype=float)
        np.testing.assert_array_equal(probe.MIC_XY, expected[:, :2])
        self.assertEqual(expected.shape, (4, 3))
        np.testing.assert_array_equal(expected[:, 2], np.zeros(4))

    def test_azimuth_sign_and_quadrants(self):
        for direction in (-120, -45, 0, 40, 90, 155):
            with self.subTest(direction=direction):
                audio = delayed_broadband(direction)
                angles, score, used = probe.music_spectrum(audio, 16000)
                self.assertGreater(used, 0)
                self.assertLessEqual(circular_error(float(angles[np.argmax(score)]), direction), 8)

    def test_four_source_request_uses_nonempty_srp_phat_spectrum(self):
        # A four-mic MUSIC noise subspace has dimension zero at four sources.
        # The explicit PHAT fallback must still produce directional evidence.
        audio = delayed_broadband(40)
        angles, spectrum, bins = probe.music_spectrum(audio, 16000, sources=4)
        self.assertGreater(bins, 0)
        self.assertTrue(np.isfinite(spectrum).all())
        self.assertGreater(float(spectrum.max() - spectrum.min()), 0)
        self.assertLessEqual(circular_error(float(angles[np.argmax(spectrum)]), 40), 12)

    def test_explicit_srp_phat_single_source_direction(self):
        audio = delayed_broadband(40, seed=19)
        angles, spectrum, used = probe.music_spectrum(
            audio, 16000, sources=1, algorithm="srp-phat")
        self.assertGreater(used, 0)
        self.assertLessEqual(circular_error(float(angles[np.argmax(spectrum)]), 40), 8)

    def test_explicit_music_rejects_four_sources_without_noise_subspace(self):
        with self.assertRaisesRegex(ValueError, "cannot resolve four"):
            probe.music_spectrum(np.zeros((2048, 4)), 16000,
                                 sources=4, algorithm="normmusic")

    def test_silence_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "no non-silent"):
            probe.music_spectrum(np.zeros((2048, 4)), 16000)

    def test_insufficient_frames_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "at least eight"):
            probe.music_spectrum(np.zeros((512, 4)), 16000)

    def test_invalid_source_hypothesis_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "sources must"):
            probe.music_spectrum(np.zeros((2048, 4)), 16000, sources=5)

    def test_windowed_direction_change_and_silence(self):
        first = delayed_broadband(-60, seed=10, count=8192)
        second = delayed_broadband(75, seed=11, count=8192)
        silent = np.zeros((8192, 4))
        audio = np.concatenate((first, silent, second), axis=0)
        windows = probe.windowed_music(audio, 16000, 0.512, 0.512)
        self.assertEqual(len(windows), 3)
        self.assertEqual(windows[1]["status"], "silent")
        self.assertIsNone(windows[1]["peak_azimuth_deg"])
        self.assertLessEqual(circular_error(windows[0]["peak_azimuth_deg"], -60), 8)
        self.assertLessEqual(circular_error(windows[2]["peak_azimuth_deg"], 75), 8)
        self.assertEqual(windows[2]["start_sample"], 16384)

    def test_windowed_invalid_step_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "positive"):
            probe.windowed_music(np.zeros((8192, 4)), 16000, 0.5, 0)

    def test_cli_silent_t00_emits_no_direction(self):
        # Actual six-channel PCM16 fixture; no invented detection on silence.
        with tempfile.TemporaryDirectory() as tmp:
            wav_path = Path(tmp) / "t00_silent.wav"
            out_path = Path(tmp) / "ssl.json"
            with wave.open(str(wav_path), "wb") as out:
                out.setnchannels(6)
                out.setsampwidth(2)
                out.setframerate(16000)
                out.writeframes(np.zeros((16000, 6), dtype="<i2").tobytes())
            subprocess.run([sys.executable, str(HERE / "acoustix_normmusic_probe.py"),
                            str(wav_path), "--output", str(out_path)], check=True)
            result = json.loads(out_path.read_text(encoding="utf-8"))
            self.assertEqual(result["global_status"], "silent")
            self.assertIsNone(result["peak_azimuth_deg"])
            self.assertEqual(result["frequency_bins"], 0)
            self.assertTrue(result["windows"])
            self.assertTrue(all(w["status"] == "silent" and
                                w["peak_azimuth_deg"] is None
                                for w in result["windows"]))

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
