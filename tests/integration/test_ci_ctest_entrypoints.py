#!/usr/bin/env python3
"""Regression contract for CTest-only test execution in the main CI workflow."""
from __future__ import annotations

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"
TEST_CMAKE = ROOT / "tests" / "CMakeLists.txt"
CORPUS_WORKFLOW = ROOT / ".github" / "workflows" / "voxconverse-benchmark.yml"


class CiCTestEntrypointTests(unittest.TestCase):
    def test_every_ci_ctest_call_fails_if_no_tests_match(self):
        lines = WORKFLOW.read_text(encoding="utf-8").splitlines()
        invocations = [(number, line.strip()) for number, line in enumerate(lines, 1)
                       if re.search(r"\bctest\s+--", line)]
        self.assertGreater(len(invocations), 70)
        missing = [(number, line) for number, line in invocations
                   if "--no-tests=error" not in line]
        self.assertEqual(missing, [],
                         "CTest selectors must not pass on an empty test set")

    def test_ci_does_not_directly_invoke_test_runners(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        # Python scoring assertions belong to add_test(), not ad-hoc commands.
        self.assertNotRegex(
            workflow,
            r"(?m)^\s*(?:python3?|[^\s]+/python)\s+"
            r"tests/(?:integration/)?(?:score_|test_)[\w-]+\.py\b")
        # Executable test binaries must likewise be invoked by CTest.
        self.assertNotRegex(
            workflow,
            r"(?m)^\s*(?:\./)?build[\w/-]*/tests/"
            r"\w[\w.-]*(?:\s|$)")

    def test_heavy_corpus_workflow_uses_ctest_for_selftests(self):
        workflow = CORPUS_WORKFLOW.read_text(encoding="utf-8")
        self.assertNotRegex(
            workflow, r"(?m)^\s*python3?\s+tests/integration/[\w-]+\.py\s+--self-test")
        self.assertIn("ctest --no-tests=error --test-dir build-voxconverse", workflow)
        for name in ("voxconverse_full_split_preflight_selftest",
                     "diarization_scoring_selftest",
                     "diarization_corpus_scoring_selftest",
                     "voxconverse_shard_aggregation_selftest"):
            self.assertIn(name, workflow)


    def test_real_spatial_quality_requires_pinned_data_and_ctest(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        cmake = TEST_CMAKE.read_text(encoding="utf-8")
        self.assertIn("  real-spatial-corpus:", workflow)
        self.assertIn("vars.LIBAUDITION_REAL_CORPUS_URL", workflow)
        self.assertIn("vars.LIBAUDITION_REAL_CORPUS_SHA256", workflow)
        self.assertIn("stage_real_spatial_corpus.py", workflow)
        self.assertIn("--sha256", workflow)
        self.assertIn(
            "ctest --no-tests=error --test-dir build-real-odas "
            "-R '^audio_real_respeaker_corpus$' --output-on-failure",
            workflow)
        self.assertIn("add_test(NAME audio_real_spatial_bundle_selftest", cmake)
        self.assertIn("add_test(NAME audio_real_respeaker_corpus", cmake)
        self.assertIn("LIBAUDITION_REAL_CORPUS_MAX_MEDIAN_ERROR_DEG", cmake)
        self.assertIn("LIBAUDITION_REAL_CORPUS_MIN_TRACK_COVERAGE", cmake)

        # The optional corpus job must be isolated from its neighbors.
        self.assertEqual(workflow.count("  real-spatial-corpus:\n"), 1)
        self.assertEqual(workflow.count("  odas-backend:\n"), 1)
        self.assertEqual(workflow.count("  gtsam-backend:\n"), 1)
        start = workflow.index("  real-spatial-corpus:\n")
        end = workflow.index("  odas-backend:\n", start)
        real_job = workflow[start:end]
        self.assertEqual(real_job.count("    runs-on: ubuntu-latest"), 1)

        # Any configured corpus setting must enable the optional gate,
        # including additional threshold-only configurations. A partial
        # configuration then fails at the missing required-input checks
        # instead of quietly skipping the real recording test.
        required_settings = {
            "LIBAUDITION_REAL_CORPUS_URL",
            "LIBAUDITION_REAL_CORPUS_SHA256",
            "LIBAUDITION_REAL_CORPUS_MAX_MEDIAN_ERROR_DEG",
            "LIBAUDITION_REAL_CORPUS_MIN_TRACK_COVERAGE",
            "LIBAUDITION_REAL_CORPUS_MAX_P90_ERROR_DEG",
            "LIBAUDITION_REAL_CORPUS_MAX_ASSOCIATION_ERROR_DEG",
            "LIBAUDITION_REAL_CORPUS_MAX_ADJACENT_ID_SWITCHES",
            "LIBAUDITION_REAL_CORPUS_MIN_SIMULTANEOUS_TRACK_COVERAGE",
            "LIBAUDITION_REAL_CORPUS_MAX_SILENT_FALSE_POSITIVE_FRACTION",
        }
        match = re.search(r"(?m)^    if: >-\n(.*?)(?=^    runs-on:)",
                          real_job, flags=re.DOTALL)
        self.assertIsNotNone(match, "Real-corpus job requires a folded activation guard")
        guard = match.group(1)
        terms = set(re.findall(
            r"vars\.(LIBAUDITION_REAL_CORPUS_[A-Z0-9_]+)\s*!=\s*''",
            guard))
        self.assertEqual(terms, required_settings,
                         "Every real-corpus variable must activate the CI job")
        self.assertEqual(guard.count("||"), len(required_settings) - 1,
                         "Corpus settings must be joined as logical OR")
        for name in required_settings:
            # A single configured value must turn the gate on.
            configured = {key: (key == name) for key in required_settings}
            self.assertTrue(any(configured[key] for key in terms),
                            f"Partial configuration for {name} was skipped")
        for needle in (
            '${CORPUS_URL:?Set LIBAUDITION_REAL_CORPUS_URL',
            '${CORPUS_SHA256:?Set LIBAUDITION_REAL_CORPUS_SHA256',
            '${LIBAUDITION_REAL_CORPUS_MAX_MEDIAN_ERROR_DEG:?',
            '${LIBAUDITION_REAL_CORPUS_MIN_TRACK_COVERAGE:?',
        ):
            self.assertIn(needle, real_job)

        self.assertIn(
            "run: ctest --no-tests=error --test-dir build-real-odas "
            "-R '^audio_real_respeaker_corpus$' --output-on-failure",
            real_job)
        self.assertIn("name: libaudition-real-spatial-quality", real_job)
        self.assertTrue(real_job.endswith("\n\n"))

    def test_real_diarization_rttm_checks_have_ctest_fixtures(self):
        cmake = TEST_CMAKE.read_text(encoding="utf-8")
        for name in (
            "speech_real_speaker_diarization_rttm_syntax",
            "speech_real_speaker_diarization_benchmark_rttm_syntax",
        ):
            self.assertIn(f"add_test(NAME {name}", cmake)
        self.assertIn("FIXTURES_SETUP speech_diarization_report", cmake)
        self.assertIn("FIXTURES_SETUP speech_diarization_benchmark_report", cmake)
        self.assertIn("FIXTURES_REQUIRED speech_diarization_report", cmake)
        self.assertIn("FIXTURES_REQUIRED speech_diarization_benchmark_report", cmake)


if __name__ == "__main__":
    unittest.main()
