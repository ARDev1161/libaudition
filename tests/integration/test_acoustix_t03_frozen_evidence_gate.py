#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location("gate",Path(__file__).with_name("acoustix_t03_frozen_evidence_gate.py"))
gate=importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def fixture():
    hops=[{"hop":i,"stage":"ssl_hit_sst_miss" if i<214 else "ssl_and_sst_miss",
           "ssl_candidates":4 if i<117 else 3,"sst_tracks":1} for i in range(243)]
    # 117*4 + 126*3 = 846 proposals
    return {"schema":"libaudition.t03_ssl_sst_failure_localization.v1",
            "threshold_deg":45.0, **gate.EXPECTED,
            "ssl_proposals_in_overlap":846,"hop_attribution":hops}


class FrozenEvidenceGateTests(unittest.TestCase):
    def test_known_failure_is_not_quality_pass(self):
        result=gate.verify(fixture())
        self.assertEqual(result["status"],"KNOWN_SST_CAPACITY_FAILURE")

    def test_two_tracks_break_frozen_baseline(self):
        report=fixture()
        report["hop_attribution"][0]["sst_tracks"]=2
        with self.assertRaisesRegex(ValueError,"exactly one"):
            gate.verify(report)

    def test_missing_hop_fails(self):
        report=fixture()
        report["hop_attribution"][1]["hop"]=0
        with self.assertRaisesRegex(ValueError,"duplicate"):
            gate.verify(report)

    def test_changed_tolerance_fails(self):
        report=fixture()
        report["threshold_deg"]=90.
        with self.assertRaisesRegex(ValueError,"tolerance"):
            gate.verify(report)

    def test_proposal_accounting_fails(self):
        report=fixture()
        report["ssl_proposals_in_overlap"]=845
        with self.assertRaisesRegex(ValueError,"proposal accounting"):
            gate.verify(report)


if __name__=="__main__":
    unittest.main()
