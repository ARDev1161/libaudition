#!/usr/bin/env python3
"""Synthetic unit contracts only; no native acoustic quality claim."""
import importlib.util
from pathlib import Path
import unittest
import tempfile

spec=importlib.util.spec_from_file_location("loc",Path(__file__).with_name("acoustix_t03_ssl_sst_localization.py"))
loc=importlib.util.module_from_spec(spec)
spec.loader.exec_module(loc)


def voice(id,deg):
    return {"id":id,"kind":"voice","bearing_deg":deg,"elevation_deg":0}

def candidate(id,deg):
    import math
    a=math.radians(deg)
    return {"id":str(id),"x":str(math.cos(a)),"y":str(math.sin(a)),
            "z":"0","activity":"1"}

def oracle():
    return [{"hop":0,"sources":[voice("a",0),voice("b",90)]}]


class LocalizationTests(unittest.TestCase):
    def test_ssl_has_two_sst_loses_second(self):
        obs={0:{"proposal":[candidate(1,0),candidate(2,90)],
                "track":[candidate(1,0)]}}
        d=loc.diagnose(oracle(),obs)
        self.assertEqual((d["ssl_two_matches"],d["sst_two_matches"],d["ssl_hit_sst_miss"]),(1,0,1))

    def test_ssl_loses_second(self):
        obs={0:{"proposal":[candidate(1,0)],"track":[candidate(1,0)]}}
        d=loc.diagnose(oracle(),obs)
        self.assertEqual((d["ssl_miss"],d["ssl_hit_sst_miss"]),(1,0))

    def test_two_proposals_same_direction_do_not_match(self):
        obs={0:{"proposal":[candidate(1,0),candidate(2,0)],"track":[]}}
        self.assertEqual(loc.diagnose(oracle(),obs)["ssl_two_matches"],0)

    def test_two_sources_and_tracks(self):
        obs={0:{"proposal":[candidate(1,0),candidate(2,90)],
                "track":[candidate(1,0),candidate(2,90)]}}
        self.assertEqual(loc.diagnose(oracle(),obs)["sst_two_matches"],1)

    def test_repeated_proposal_slot_labels_are_not_track_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "observations.csv"
            p.write_text("hop,kind,id,x,y,z,activity\n"
                         "0,proposal,0,1,0,0,1\n"
                         "0,proposal,0,0,1,0,1\n"
                         "0,track,7,1,0,0,1\n")
            observations = loc.load_observations(p, 1)
            self.assertEqual(loc.diagnose(oracle(), observations)["ssl_two_matches"], 1)

    def test_duplicate_active_track_ids_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "observations.csv"
            p.write_text("hop,kind,id,x,y,z,activity\n"
                         "0,track,7,1,0,0,1\n"
                         "0,track,7,0,1,0,1\n")
            with self.assertRaisesRegex(ValueError, "duplicate native track"):
                loc.load_observations(p, 1)

    def test_duplicate_oracle_voice_rejected(self):
        bad=[{"hop":0,"sources":[voice("a",0),voice("a",90)]}]
        with self.assertRaisesRegex(ValueError,"duplicate"):
            loc.diagnose(bad,{0:{"proposal":[candidate(1,0),candidate(2,90)],"track":[]}})


if __name__=="__main__":
    unittest.main()
