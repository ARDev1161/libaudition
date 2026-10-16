#!/usr/bin/env python3
"""Archive-integrity contract tests; fake WAV is not evidence of acoustic quality."""
from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import stat
import tempfile
import unittest
import wave
import zipfile

from stage_real_spatial_corpus import stage_archive, digest


class RealSpatialCorpusStageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.archive = self.root / "corpus.zip"
        self.target = self.root / "corpus"
        sink = io.BytesIO()
        with wave.open(sink, "wb") as writer:
            writer.setnchannels(4)
            writer.setsampwidth(2)
            writer.setframerate(16000)
            writer.writeframes(bytes(128 * 4 * 2 * 20))
        self.wav = sink.getvalue()
        self.rec = {
            "id": "test01", "wav": "test01.wav",
            "sha256": hashlib.sha256(self.wav).hexdigest(),
            "provenance": {"dataset": "synthetic-validator-fixture-NOT-real"},
            "intervals": [
                {"start_s": 0, "end_s": 0.08, "direction_xyz": [1, 0, 0]}
            ],
        }
        self.doc = {
            "schema": "libaudition.respeaker-corpus.v1",
            "array": {"microphones": [
                {"channel": 0, "xyz_m": [0, 0, 0]},
                {"channel": 1, "xyz_m": [0.03, 0, 0]},
                {"channel": 2, "xyz_m": [0, 0.03, 0]},
                {"channel": 3, "xyz_m": [-0.03, 0, 0]},
            ]},
            "recordings": [self.rec],
        }

    def pack(self, extras=None):
        files = {"corpus.json": json.dumps(self.doc).encode(),
                 "test01.wav": self.wav}
        files.update(extras or {})
        with zipfile.ZipFile(self.archive, "w",
                             compression=zipfile.ZIP_DEFLATED) as z:
            for name, value in files.items():
                z.writestr(name, value)
        return digest(self.archive)

    def assert_refused(self, pattern, *, sha=None, **kwargs):
        if sha is None:
            sha = digest(self.archive)
        with self.assertRaisesRegex(ValueError, pattern):
            stage_archive(self.archive, sha, self.target, **kwargs)
        self.assertFalse(self.target.exists())

    def test_stages_matching_bytes_and_manifest(self):
        sha = self.pack()
        self.assertEqual(stage_archive(self.archive, sha, self.target)
                         ["recordings"], 1)
        self.assertEqual((self.target / "test01.wav").read_bytes(), self.wav)
        self.assertEqual(json.loads((self.target / "corpus.json").read_text()),
                         self.doc)
        with self.assertRaisesRegex(ValueError, "already exists"):
            stage_archive(self.archive, sha, self.target)

    def test_archive_digest_mismatch(self):
        sha = self.pack()
        with self.archive.open("ab") as writer:
            writer.write(b"TAMPERED")
        self.assert_refused("archive SHA-256 mismatch", sha=sha)

    def test_rejects_unexpected_paths_and_unreferenced_files(self):
        for path in ("../outside.wav", "nested/test.wav", "/abs.wav",
                     "unreferenced.wav"):
            with self.subTest(path=path):
                self.pack({path: b"bad"})
                self.assert_refused("Unsafe or unexpected|Unreferenced")
        self.assertFalse((self.root / "outside.wav").exists())

    def test_refuses_missing_or_corrupt_wav(self):
        self.rec["sha256"] = "0" * 64
        self.pack()
        self.assert_refused("WAV SHA-256 mismatch")
        self.rec["sha256"] = hashlib.sha256(self.wav).hexdigest()
        self.rec["wav"] = "absent.wav"
        self.pack()
        self.assert_refused("Referenced WAV missing")

    def test_rejects_invalid_geometry_or_missing_provenance(self):
        self.doc["array"]["microphones"][0]["channel"] = 99
        self.pack()
        self.assert_refused("outside WAV")
        self.doc["array"]["microphones"][0]["channel"] = 0
        self.rec.pop("provenance")
        self.pack()
        self.assert_refused("provenance")

    def test_rejects_bad_reference_time(self):
        self.rec["intervals"][0]["end_s"] = 10.0
        self.pack()
        self.assert_refused("exceeds WAV duration")

    def test_rejects_symlink_entry(self):
        self.pack()
        link = zipfile.ZipInfo("alias.wav")
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        with zipfile.ZipFile(self.archive, "a") as z:
            z.writestr(link, "test01.wav")
        self.assert_refused("symlink or special file")

    def test_rejects_case_colliding_names(self):
        self.pack({"TEST01.wav": self.wav})
        self.assert_refused("Duplicate ZIP path")

    def test_rejects_zip_expansion_over_limit(self):
        self.pack()
        self.assert_refused("expanded size exceeds limit", max_expanded=100)


if __name__ == "__main__":
    unittest.main()
