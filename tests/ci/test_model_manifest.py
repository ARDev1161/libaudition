#!/usr/bin/env python3
"""Black-box positive and negative cases for CI model-manifest audit."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CHECKER = Path(__file__).with_name("check_model_manifest.py")
DIGEST = "a" * 64
OTHER = "b" * 64


class ModelManifestAuditTests(unittest.TestCase):
    def check(self, manifest, workflow, ok):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            m = root / "manifest.yaml"
            w = root / "ci.yml"
            m.write_text(manifest, encoding="utf-8")
            w.write_text(workflow, encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(CHECKER), str(m), str(w)],
                capture_output=True, text=True, check=False
            )
            self.assertEqual(result.returncode == 0, ok, result.stdout + result.stderr)

    @staticmethod
    def manifest(digest=DIGEST, name="model.onnx", model_id="fixture", bundled="false"):
        return (
            "schema_version: 1\nmodels:\n"
            f"  - id: {model_id}\n"
            f"    source: https://example.test/resolve/main/{name}\n"
            "    source_revision: UNPINNED-main\n"
            f"    sha256: {digest}\n"
            f"    bundled: {bundled}\n"
        )

    def test_matching_digest_and_filename(self):
        self.check(self.manifest(), f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", True)

    def test_missing_digest(self):
        self.check(self.manifest(), f"echo '{OTHER}  model.onnx' | sha256sum -c -\n", False)

    def test_digest_belongs_to_different_file(self):
        self.check(self.manifest(), f"echo '{DIGEST}  other.onnx' | sha256sum -c -\n", False)

    def test_digest_and_filename_on_different_lines(self):
        self.check(self.manifest(), f"echo '{DIGEST}  other.onnx' | sha256sum -c -\n"
                   "curl -o model.onnx https://example.test/model.onnx\n", False)

    def test_digest_not_verified(self):
        self.check(self.manifest(), f"# {DIGEST} model.onnx\n", False)

    def test_invalid_digest(self):
        self.check(self.manifest(digest="bad"), f"echo 'bad  model.onnx' | sha256sum -c -\n", False)

    def test_duplicate_id(self):
        self.check(self.manifest() + self.manifest(model_id="fixture").split("models:\n", 1)[1],
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_bundled_fixture(self):
        self.check(self.manifest(bundled="true"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)


if __name__ == "__main__":
    unittest.main()
