#!/usr/bin/env python3
"""Self-tests for reporting measured, unexecuted branches."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).parents[2] / "tools" / "audit_gcovr_branches.py"
spec = importlib.util.spec_from_file_location("branch_audit", MODULE)
assert spec is not None and spec.loader is not None
branch_audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(branch_audit)


class BranchAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "src" / "pipeline").mkdir(parents=True)
        (self.root / "src" / "pipeline" / "foo.cpp").write_text(
            "if (ready) {\n  return 1;\n}\n", encoding="utf-8")
        (self.root / "src" / "bar.cpp").write_text("return 2;\n", encoding="utf-8")

    def report(self):
        return {"files": [
            {"file": "src/pipeline/foo.cpp", "lines": [
                {"line_number": 1, "count": 4, "branches": [
                    {"count": 4}, {"count": 0}, {"count": 0}]},
                {"line_number": 2, "count": 4, "branches": []}]},
            {"file": "src/bar.cpp", "lines": [
                {"line_number": 1, "count": 2, "branches": [{"count": 2}]}]},
        ]}

    def test_measured_uncovered_branches_are_sorted_by_gap(self):
        result = branch_audit.audit(self.report(), self.root)
        self.assertEqual((result["branch_covered"], result["branch_total"],
                          result["branch_uncovered"]), (2, 4, 2))
        self.assertEqual(result["files"][0]["file"], "src/pipeline/foo.cpp")
        location = result["files"][0]["uncovered_locations"][0]
        self.assertEqual(location["uncovered_branch_indices"], [1, 2])
        self.assertEqual(location["review_status"], "unreviewed")
        self.assertIn("if (ready)", location["source"])

    def test_markdown_does_not_claim_unreachable(self):
        doc = branch_audit.markdown(branch_audit.audit(self.report(), self.root))
        self.assertIn("src/pipeline/foo.cpp:1", doc)
        self.assertIn("not executed", doc)
        self.assertIn("unreviewed", doc)

    def test_no_branch_measurements_fail_closed(self):
        report = self.report()
        for item in report["files"]:
            for line in item["lines"]:
                line["branches"] = []
        with self.assertRaisesRegex(ValueError, "no branch measurements"):
            branch_audit.audit(report, self.root)

    def test_invalid_input_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "non-empty files"):
            branch_audit.audit({"files": []}, self.root)
        report = self.report()
        report["files"][0]["lines"][0]["branches"][0]["count"] = -1
        with self.assertRaisesRegex(ValueError, "invalid branch hits"):
            branch_audit.audit(report, self.root)

    def test_missing_source_and_outside_path_are_errors(self):
        report = self.report()
        report["files"][0]["file"] = "src/missing.cpp"
        with self.assertRaisesRegex(ValueError, "not found"):
            branch_audit.audit(report, self.root)
        report["files"][0]["file"] = "../outside.cpp"
        with self.assertRaisesRegex(ValueError, "escapes source root"):
            branch_audit.audit(report, self.root)


if __name__ == "__main__":
    unittest.main()
