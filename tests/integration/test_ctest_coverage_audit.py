#!/usr/bin/env python3
"""Regression tests for the registration-audit contract itself."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import re
import unittest

MODULE_FILE = Path(__file__).with_name("audit_ctest_algorithms.py")
spec = importlib.util.spec_from_file_location("algorithm_audit", MODULE_FILE)
assert spec is not None and spec.loader is not None
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AuditContractTests(unittest.TestCase):
    def test_all_known_library_translation_units_are_declared(self):
        report = audit.source_inventory(Path(__file__).resolve().parents[2])
        self.assertGreater(report["compiled_translation_units"], 40)
        self.assertEqual(report["untracked_compiled_sources"], [])
        self.assertEqual(report["missing_inventory_sources"], [])
        self.assertEqual(report["missing_source_files"], [])

    def test_audio_corpus_contract_is_mandatory_without_odas(self):
        required = audit.expectations({})
        self.assertIn("audio corpus manifest and independent annotations", required)
        self.assertRegex("audio_respeaker_manifest_selftest",
                         required["audio corpus manifest and independent annotations"])
        self.assertNotRegex("audio_respeaker_manifest_selftest", r"^OdasBackend\\.")

    def test_core_contract_is_not_optional(self):
        expected = audit.expectations({})
        for feature in audit.CORE:
            self.assertIn(feature, expected)

    def test_optional_backends_are_gated(self):
        default = audit.expectations({})
        for backend in audit.BACKENDS:
            marked = audit.expectations({backend: True})
            self.assertGreater(len(marked), len(default))
            self.assertTrue(any(name.startswith(backend) for name in marked))
            self.assertFalse(any(name.startswith(backend) for name in default))

    def test_real_model_contract_is_opt_in(self):
        flags = {"LIBAUDITION_WITH_YAMNET": True,
                 "LIBAUDITION_WITH_EFFICIENTAT": True}
        synthetic = audit.expectations(flags)
        assert_real = audit.expectations({**flags, "LIBAUDITION_REGISTER_MODEL_TOOL_TESTS": True})
        self.assertNotIn("real_yamnet_stress_10", " ".join(synthetic))
        self.assertTrue(any("real_yamnet_stress_10" in name for name in assert_real))
        self.assertTrue(any("real_esc10_yamnet_80" in name for name in assert_real))
        self.assertGreater(len(assert_real), len(synthetic) + 10)

    def test_every_required_pattern_is_compilable(self):
        for pattern in audit.expectations({
                key: True for key in (*audit.BACKENDS, "LIBAUDITION_REGISTER_MODEL_TOOL_TESTS",
                                     "LIBAUDITION_REGISTER_EFFICIENTAT_REFERENCE_TESTS",
                                     "LIBAUDITION_REGISTER_REAL_QT_MODEL_TESTS")
        }).values():
            re.compile(pattern)


if __name__ == "__main__":
    unittest.main()
