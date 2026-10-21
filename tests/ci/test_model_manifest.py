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
    def check(self, manifest, workflow, ok, evidence_content=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            m = root / "manifest.yaml"
            w = root / "ci.yml"
            m.write_text(manifest, encoding="utf-8")
            w.write_text(workflow, encoding="utf-8")
            if evidence_content is not None:
                legal = root / "docs/legal"
                legal.mkdir(parents=True)
                (legal / "fixture-review.md").write_text(evidence_content, encoding="utf-8")
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

    def test_missing_schema_version(self):
        self.check(self.manifest().replace("schema_version: 1\n", ""),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_unknown_schema_version(self):
        self.check(self.manifest().replace("schema_version: 1", "schema_version: 2"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_duplicate_models_section(self):
        self.check(self.manifest() + "models:\n",
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_unknown_model_field(self):
        self.check(self.manifest() + "    unchecked_fixture: true\n",
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_misindented_checksum_field(self):
        self.check(self.manifest().replace("    sha256:", "   sha256:"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_unregistered_yaml_model_entry(self):
        self.check(self.manifest() + "  - source: https://example.test/rogue.onnx\n",
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

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


    def test_release_asset_archive_checksum(self):
        manifest = self.manifest(name="voice.tar.bz2", revision="github-release-UNVERIFIED")
        manifest = manifest.replace(
            "https://example.test/resolve/main/voice.tar.bz2",
            "https://github.com/example/project/releases/download/voices/voice.tar.bz2")
        self.check(manifest,
                   f'echo "{DIGEST}  build/voice.tar.bz2" | sha256sum --check\n',
                   True)

    def test_download_alias_requires_explicit_mapping(self):
        self.check(self.manifest(name="voice.tar.bz2"),
                   f'echo "{DIGEST}  build/renamed.tar.bz2" | sha256sum --check\n', False)

    def test_download_alias_matches_checked_filename(self):
        manifest = self.manifest(name="voice.tar.bz2")
        manifest += "    ci_filename: renamed.tar.bz2\n"
        self.check(manifest,
                   f'echo "{DIGEST}  build/renamed.tar.bz2" | sha256sum --check\n', True)

    def test_download_alias_wrong_digest(self):
        manifest = self.manifest(name="voice.tar.bz2")
        manifest += "    ci_filename: renamed.tar.bz2\n"
        self.check(manifest,
                   f'echo "{OTHER}  build/renamed.tar.bz2" | sha256sum --check\n', False)

    def test_download_alias_rejects_path(self):
        manifest = self.manifest(name="voice.tar.bz2")
        manifest += "    ci_filename: ../renamed.tar.bz2\n"
        self.check(manifest,
                   f'echo "{DIGEST}  build/renamed.tar.bz2" | sha256sum --check\n', False)

    def test_release_asset_archive_wrong_checksum(self):
        manifest = self.manifest(name="voice.tar.bz2", revision="github-release-UNVERIFIED")
        manifest = manifest.replace(
            "https://example.test/resolve/main/voice.tar.bz2",
            "https://github.com/example/project/releases/download/voices/voice.tar.bz2")
        self.check(manifest,
                   f'echo "{OTHER}  build/voice.tar.bz2" | sha256sum --check\n',
                   False)

    def test_pinned_revision_valid(self):
        self.check(self.manifest(revision="deadbeef", source_ref="deadbeef"),
                   f"echo '{DIGEST}  model.onnx' | sha256sum --check\n", True)

    def test_filename_prefix_collision(self):
        self.check(self.manifest(), f"echo '{DIGEST}  other-model.onnx' | sha256sum -c -\n", False)

    def test_filename_suffix_collision(self):
        self.check(self.manifest(), f"echo '{DIGEST}  model.onnx.backup' | sha256sum -c -\n", False)

    def test_expected_name_only_in_extra_field(self):
        self.check(self.manifest(),
                   f"echo '{DIGEST}  wrong.onnx model.onnx' | sha256sum -c -\n", False)

    def test_expected_name_only_in_comment(self):
        self.check(self.manifest(),
                   f"echo '{DIGEST}  wrong.onnx # model.onnx' | sha256sum -c -\n", False)

    def test_shell_variable_path(self):
        self.check(self.manifest(),
                   f'echo "{DIGEST}  $PWD/build/model.onnx" | sha256sum --check\n', True)

    def test_concatenated_quoted_shell_path(self):
        self.check(self.manifest(),
                   f"""echo '{DIGEST}  '"$RUNNER_TEMP/build/model.onnx" | sha256sum --check\n""", True)

    def test_shell_variable_wrong_basename(self):
        self.check(self.manifest(),
                   f'echo "{DIGEST}  $PWD/build/wrong.onnx" | sha256sum --check # model.onnx\n', False)



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
        self.check(approved, f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", True, evidence_content="# Review\n")

    def test_approved_review_missing_evidence_file(self):
        approved = self.manifest(review="approved").replace(
            "    license_review: approved\n",
            "    license_review: approved\n"
            "    license_evidence: docs/legal/fixture-review.md\n")
        self.check(approved, f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_approved_review_empty_evidence_file(self):
        approved = self.manifest(review="approved").replace(
            "    license_review: approved\n",
            "    license_review: approved\n"
            "    license_evidence: docs/legal/fixture-review.md\n")
        self.check(approved, f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n",
                   False, evidence_content="")

    def test_approved_review_rejects_url_evidence(self):
        approved = self.manifest(review="approved").replace(
            "    license_review: approved\n",
            "    license_review: approved\n"
            "    license_evidence: https://example.test/license\n")
        self.check(approved, f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_approved_review_rejects_path_traversal(self):
        approved = self.manifest(review="approved").replace(
            "    license_review: approved\n",
            "    license_review: approved\n"
            "    license_evidence: docs/legal/../fixture-review.md\n")
        self.check(approved, f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n",
                   False, evidence_content="# Review\n")

    def test_approved_review_rejects_nonlegal_path(self):
        approved = self.manifest(review="approved").replace(
            "    license_review: approved\n",
            "    license_review: approved\n"
            "    license_evidence: README.md\n")
        self.check(approved, f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)

    def test_unverified_license_cannot_be_approved(self):
        approved = self.manifest(license="UNVERIFIED", review="approved")
        approved = approved.replace("    license_review: approved\n",
                                    "    license_review: approved\n"
                                    "    license_evidence: docs/legal/fixture-review.md\n")
        self.check(approved, f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n", False)


if __name__ == "__main__":
    unittest.main()

    def test_approved_fixture_cannot_use_mutable_main_revision(self):
        manifest = self.manifest(review="approved")
        manifest += "    license_evidence: docs/legal/fixture-review.md\n"
        self.check(manifest,
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n",
                   False, evidence_content="License review evidence")

    def test_approved_fixture_accepts_immutable_revision_with_evidence(self):
        manifest = self.manifest(review="approved", revision="deadbeef", source_ref="deadbeef")
        manifest += "    license_evidence: docs/legal/fixture-review.md\n"
        self.check(manifest,
                   f"echo '{DIGEST}  model.onnx' | sha256sum -c -\n",
                   True, evidence_content="License review evidence")
