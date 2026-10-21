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
    def manifest(digest=DIGEST, name="model.onnx", model_id="fixture", bundled="false",
                 revision="UNPINNED-main", source_ref="main", license="Apache-2.0",
                 commercial="false", review="pending"):
        return (
            "schema_version: 1\nmodels:\n"
            f"  - id: {model_id}\n"
            "    backend: onnxruntime\n"
            "    task: audio_event_classification\n"
            f"    source: https://example.test/resolve/{source_ref}/{name}\n"
            f"    source_revision: {revision}\n"
            f"    sha256: {digest}\n"
            f"    license: {license}\n"
            f"    license_review: {review}\n"
            f"    commercial: {commercial}\n"
            "    redistributable: false\n"
            f"    bundled: {bundled}\n"
            "    purpose: ci-only fixture\n"
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


    def test_pinned_revision_valid(self):
        self.check(self.manifest(revision="deadbeef", source_ref="deadbeef"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum --check\n", True)

    def test_filename_prefix_collision(self):
        self.check(self.manifest(), f"echo '{DIGEST}  other-model.onnx' | sha256sum -c -\n", False)

    def test_filename_suffix_collision(self):
        self.check(self.manifest(), f"echo '{DIGEST}  model.onnx.backup' | sha256sum -c -\n", False)

    def test_hash_in_comment(self):
        self.check(self.manifest(), f"# echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_revision_mismatch_with_main(self):
        self.check(self.manifest(revision="deadbeef"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_revision_mismatch_with_commit(self):
        self.check(self.manifest(revision="deadbeef", source_ref="cafebabe"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_unverified_license_needs_review(self):
        self.check(self.manifest(license="UNVERIFIED", review="approved"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_invalid_commercial_permission(self):
        self.check(self.manifest(commercial="unknown"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)


    def test_pending_review_cannot_claim_commercial_permission(self):
        self.check(self.manifest(commercial="true"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_pending_review_cannot_claim_redistribution_permission(self):
        self.check(self.manifest().replace("redistributable: false",
                                           "redistributable: true"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_missing_review_is_rejected(self):
        self.check(self.manifest().replace("    license_review: pending\n", ""),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_unknown_review_state_is_rejected(self):
        self.check(self.manifest(review="unknown"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_approved_review_requires_evidence(self):
        self.check(self.manifest(review="approved"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_approved_review_with_evidence(self):
        approved = self.manifest(review="approved", commercial="true")
        approved = approved.replace("    license_review: approved\n",
                                    "    license_review: approved\n"
                                    "    license_evidence: docs/legal/fixture-review.md\n")
        self.check(approved, f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", True)

    def test_unverified_license_cannot_be_approved(self):
        approved = self.manifest(license="UNVERIFIED", review="approved")
        approved = approved.replace("    license_review: approved\n",
                                    "    license_review: approved\n"
                                    "    license_evidence: docs/legal/fixture-review.md\n")
        self.check(approved, f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)


if __name__ == "__main__":
    unittest.main()
