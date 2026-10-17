#!/usr/bin/env python3
"""Static LOCATA converter contracts, independent of a downloaded dataset."""
import hashlib
import json
import math
import shutil
import wave
from unittest.mock import patch
from pathlib import Path
import tempfile
import unittest

from prepare_locata_static import (
    locata_rows, time_value, take_static_poses, static_reference, local_vector,
    prepare,
)


def row(second, *, array=True, source_x=12., rotation=False, drift=0.):
    r = {"year": 2026, "month": 10, "day": 17, "hour": 1, "minute": 0,
         "second": second}
    r.update({"x": 10. + drift if array else source_x,
              "y": 20., "z": 1.})
    mat = ((0, -1, 0), (1, 0, 0), (0, 0, 1)) if rotation else (
        (1, 0, 0), (0, 1, 0), (0, 0, 1))
    for i in range(3):
        for j in range(3):
            r[f"rotation_{i+1}{j+1}"] = mat[i][j]
    if array:
        for index, xyz in enumerate(((10. - .04, 20., 1.),
                                     (10. + .04, 20., 1.),
                                     (10., 20. - .04, 1.),
                                     (10., 20. + .04, 1.)), 1):
            for axis, coord in zip("xyz", xyz):
                r[f"mic{index}_{axis}"] = coord
    return r


def write_table(path, rows, sep="\t"):
    keys = list(rows[0])
    path.write_text(sep.join(keys) + "\n" +
                    "\n".join(sep.join(str(r[key]) for key in keys)
                              for r in rows) + "\n", encoding="utf-8")


class LocataStaticConverterTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.start = time_value(row(0.0))
        self.seconds = [0.0, 0.016, 0.032, 0.048, 0.064]

    def sample_rows(self, array=True, **kwargs):
        return [row(second, array=array, **kwargs) for second in self.seconds]

    def test_parse_tsv_comma_and_space_tables(self):
        for delim in ("\t", ",", " "):
            f = self.root / "position.txt"
            rows = self.sample_rows()
            write_table(f, rows, delim)
            parsed = list(locata_rows(f))
            self.assertEqual(len(parsed), 5)
            self.assertEqual(float(parsed[0]["mic1_x"]), 9.96)

    def test_clock_is_relative_not_local_timezone(self):
        a = time_value(row(59.5))
        b = time_value({**row(0.5), "minute": 1})
        self.assertAlmostEqual(b-a, 1.0)

    def test_world_to_array_local_and_true_3d_bearing(self):
        a = [(self.start + sec, r)
             for sec, r in zip(self.seconds, self.sample_rows())]
        src = self.sample_rows(array=False)
        for entry in src:
            entry["z"] = 2.
        b = [(self.start + sec, r)
             for sec, r in zip(self.seconds, src)]
        geometry, bearing = static_reference(a, b)
        self.assertEqual([mic["channel"] for mic in geometry["microphones"]],
                         [0, 1, 2, 3])
        self.assertAlmostEqual(geometry["microphones"][0]["xyz_m"][0], -.04)
        self.assertAlmostEqual(geometry["microphones"][2]["xyz_m"][1], -.04)
        self.assertAlmostEqual(bearing[0], 2 / math.sqrt(5))
        self.assertAlmostEqual(bearing[2], 1 / math.sqrt(5))
        self.assertAlmostEqual(sum(v*v for v in bearing), 1)

    def test_rotation_uses_transpose_not_world_orientation(self):
        rot = ((0., -1., 0.), (1., 0., 0.), (0., 0., 1.))
        self.assertEqual(local_vector(rot, (0., 2., 0.)), (2., 0., 0.))

    def test_moving_source_rejected(self):
        a = [(self.start + t, row(t)) for t in self.seconds]
        b = [(self.start + t, row(t, array=False,
                                  source_x=12. + i * .05))
             for i, t in enumerate(self.seconds)]
        with self.assertRaisesRegex(ValueError, "source is moving"):
            static_reference(a, b)

    def test_moving_array_rejected(self):
        a = [(self.start + t, row(t, drift=i * .02))
             for i, t in enumerate(self.seconds)]
        b = [(self.start + t, row(t, array=False)) for t in self.seconds]
        with self.assertRaisesRegex(ValueError, "array is moving"):
            static_reference(a, b)

    def test_pose_time_alignment_fails_closed(self):
        p = self.root / "source.txt"
        write_table(p, self.sample_rows(array=False))
        expected = take_static_poses(p, self.start, self.start + .064, "source")
        self.assertEqual(len(expected), 5)
        # There are no pose observations anywhere near this time window.
        # The converter must fail before interpreting a missing trajectory.
        with self.assertRaisesRegex(ValueError, "Too few LOCATA poses"):
            take_static_poses(p, self.start + 20, self.start + 21, "source")

        # Samples are available at the beginning of this long interval,
        # but cannot cover its end. This must fail the separate temporal
        # coverage check rather than pass because >=2 poses were found.
        with self.assertRaisesRegex(ValueError, "do not cover"):
            take_static_poses(p, self.start, self.start + 10, "source")

    def test_end_to_end_manifest_uses_measured_optitrack_direction(self):
        # Synthetic metadata/audio are used only to test the converter
        # contract; this does not qualify as a real recording quality test.
        directory = self.root / "task1" / "recording1" / "benchmark2"
        directory.mkdir(parents=True)
        source = directory / "audio_array_benchmark2.wav"
        with wave.open(str(source), "wb") as wav:
            wav.setnchannels(4)
            wav.setsampwidth(2)
            wav.setframerate(16000)
            wav.writeframes(bytes(1024 * 4 * 2))
        write_table(directory / "audio_array_timestamps_benchmark2.txt",
                    [row(0.0)])
        write_table(directory / "position_array_benchmark2.txt",
                    self.sample_rows())
        write_table(directory / "position_source_speaker1.txt",
                    self.sample_rows(array=False))
        corpus = self.root / "out-corpus"
        with patch("prepare_locata_static.extract_subclip",
                   side_effect=lambda input_path, output_path, *args:
                       shutil.copyfile(input_path, output_path)):
            result = prepare(directory, "benchmark2", "speaker1",
                             0.0, 0.064, "locata_test_static", corpus)
        self.assertAlmostEqual(result["direction_xyz"][0], 1.0)
        self.assertAlmostEqual(result["direction_xyz"][2], 0.0)
        manifest = json.loads((corpus / "corpus.json").read_text())
        self.assertEqual(len(manifest["recordings"]), 1)
        recording = manifest["recordings"][0]
        self.assertEqual(recording["intervals"][0]["direction_xyz"],
                         result["direction_xyz"])
        self.assertEqual(recording["provenance"]["dataset"], "LOCATA")
        self.assertEqual(recording["provenance"]["source"],
                         "speaker1")
        wav_path = corpus / recording["wav"]
        self.assertEqual(wav_path.read_bytes(), source.read_bytes())
        self.assertEqual(hashlib.sha256(wav_path.read_bytes()).hexdigest(),
                         recording["sha256"])

    def test_nonfinite_and_malformed_rows_rejected(self):
        p = self.root / "bad.txt"
        bad = self.sample_rows()
        bad[0]["mic1_x"] = "nan"
        write_table(p, bad)
        a = list(locata_rows(p))
        b = [(self.start + t, row(t, array=False)) for t in self.seconds]
        with self.assertRaisesRegex(ValueError, "Nonfinite microphone"):
            static_reference([(self.start + t, entry)
                              for t, entry in zip(self.seconds, a)], b)
        bad[0]["second"] = "NaN"
        write_table(p, bad)
        with self.assertRaisesRegex(ValueError, "Invalid LOCATA timestamp"):
            time_value(next(locata_rows(p)))
        bad[0]["second"] = 0
        bad[0]["rotation_11"] = 2
        write_table(p, bad)
        with self.assertRaisesRegex(ValueError, "rotation norm"):
            static_reference([(self.start + t, entry)
                              for t, entry in zip(self.seconds, locata_rows(p))], b)


if __name__ == "__main__":
    unittest.main()
