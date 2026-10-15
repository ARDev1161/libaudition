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
