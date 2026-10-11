#!/usr/bin/env python3
"""Offline tests for MID-based ontology auditing."""
import importlib.util
from pathlib import Path
import tempfile
import csv

path=Path(__file__).resolve().parents[1]/"tools/audit_audioset_vocabularies.py"
spec=importlib.util.spec_from_file_location("audio_vocab_audit",path)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as folder:
    f=Path(folder)
    for name,count in (("efficient.csv",527),("yamnet.csv",521)):
        with (f/name).open("w",newline="",encoding="utf-8") as stream:
            writer=csv.writer(stream)
            writer.writerow(["index","mid","display_name"])
            for i in range(count):
                writer.writerow([i,f"/m/id{i}",f"class {i}" if name=="efficient.csv" else f"YAMNet class {i}"])
    eff=module.load(f/"efficient.csv",527)
    yam=module.load(f/"yamnet.csv",521)
    result=module.report(eff,yam)
    assert result["common_mids"]==521
    assert len(result["efficientat_only"])==6
    assert len(result["yamnet_only"])==0
    assert len(result["renamed_common"])==521
    assert result["renamed_common"][0]["mid"]=="/m/id0"
    # Duplicate MID must be rejected even with nominally correct CSV length.
    invalid=f/"invalid.csv"
    data=(f/"yamnet.csv").read_text(encoding="utf-8")
    invalid.write_text(data.replace("/m/id1","/m/id0"),encoding="utf-8")
    try:
        module.load(invalid,521)
    except ValueError as ex:
        assert "duplicated MID" in str(ex)
    else:
        raise AssertionError("Duplicate MID unexpectedly accepted")
print("PASS: AudioSet MID ontology and duplicate detection")
