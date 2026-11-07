#!/usr/bin/env python3
"""Fast stdlib tests for the Acoustix source-stem mixing contract."""
from __future__ import annotations

from array import array
from pathlib import Path
import tempfile
import unittest
import wave

from acoustix_signal_contract import audit_audio


def write_wav(path: Path, channels: int, frames: list[tuple[int, ...]]) -> None:
    data = array("h", (x for frame in frames for x in frame))
    with wave.open(str(path), "wb") as writer:
        writer.setnchannels(channels)
        writer.setsampwidth(2)
        writer.setframerate(16000)
        writer.writeframes(data.tobytes())


class SignalContractTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "stems").mkdir()
        self.a = [(10, 20, 30, 40), (11, 21, 31, 41), (0, 0, 0, 0)]
        self.b = [(1, 2, 3, 4), (3, 4, 5, 6), (0, 0, 0, 0)]
        self.write(self.a, self.b)

    def write(self, a, b, mixed=None):
        write_wav(self.root / "stems" / "one.wav", 4, a)
        write_wav(self.root / "stems" / "two.wav", 4, b)
        if mixed is None:
            mixed = [tuple(x + y for x, y in zip(u, v))
                     for u, v in zip(a, b)]
        write_wav(self.root / "input.wav", 6,
                  [(0, *row, 0) for row in mixed])

    def run_audit(self):
        return audit_audio(self.root / "input.wav", self.root / "stems",
                           ["one", "two"], chunk_frames=2)

    def test_exact_reconstruction_and_tail(self):
        report = self.run_audit()
        self.assertEqual(report["reconstruction_max_residual_lsb"], 0)
        self.assertEqual(report["post_acoustic_signal_silence_frames"], 1)
        self.assertGreater(report["microphone_rms_pcm16"][0], 0)
        self.assertTrue(all(x > 0 for x in report["adjacent_microphone_difference_rms_pcm16"]))

    def test_pcm16_quantization_roundoff(self):
        mixed = [tuple(x + y + 1 for x, y in zip(u, v))
                 for u, v in zip(self.a, self.b)]
        self.write(self.a, self.b, mixed)
        self.assertEqual(self.run_audit()["reconstruction_max_residual_lsb"], 1)

    def test_detects_unexplained_mixture_energy(self):
        mixed = [tuple(x + y + 100 for x, y in zip(u, v))
                 for u, v in zip(self.a, self.b)]
        self.write(self.a, self.b, mixed)
        with self.assertRaisesRegex(ValueError, "Mixture and source stems disagree"):
            self.run_audit()

    def test_detects_duplicate_microphone_channels(self):
        a = [(10, 10, 10, 10)] * 2
        b = [(1, 1, 1, 1)] * 2
        self.write(a, b)
        with self.assertRaisesRegex(ValueError, "identical"):
            self.run_audit()

    def test_detects_truncated_stem(self):
        write_wav(self.root / "stems" / "two.wav", 4, self.b[:-1])
        with self.assertRaisesRegex(ValueError, "stem geometry or length"):
            self.run_audit()

    def test_empty_input_is_rejected_before_rms_division(self):
        self.write([], [])
        with self.assertRaisesRegex(ValueError, "at least one frame"):
            self.run_audit()

    def test_source_ids_cannot_escape_stem_directory(self):
        for unsafe in ("../one", "one/../two", "/tmp/one", "one\\\\two",
                       "one.wav", "UPPER", "", "."):
            with self.subTest(source_id=unsafe):
                with self.assertRaisesRegex(ValueError, "safe filename stem"):
                    audit_audio(self.root / "input.wav", self.root / "stems",
                                [unsafe, "two"])

    def test_rejects_wrong_master_channel_geometry(self):
        write_wav(self.root / "input.wav", 2, [(1, 2)] * 3)
        with self.assertRaisesRegex(ValueError, "six-channel PCM16"):
            self.run_audit()

    def test_rejects_wrong_stem_sample_rate(self):
        with wave.open(str(self.root / "stems" / "one.wav"), "wb") as w:
            w.setnchannels(4)
            w.setsampwidth(2)
            w.setframerate(8000)
            w.writeframes(array("h", (1, 2, 3, 4) * 3).tobytes())
        with self.assertRaisesRegex(ValueError, "stem geometry or length"):
            self.run_audit()

    def test_rejects_clipped_mix_even_when_stems_reconstruct(self):
        a = [(32767, 10, 20, 30)] * 3
        b = [(0, 2, 3, 4)] * 3
        self.write(a, b)
        with self.assertRaisesRegex(ValueError, "clipped"):
            self.run_audit()

    def test_duplicate_source_identity_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unique"):
            audit_audio(self.root / "input.wav", self.root / "stems",
                        ["one", "one"])


if __name__ == "__main__":
    unittest.main()
