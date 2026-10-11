#!/usr/bin/env python3
"""End-to-end offline tests of three-model MID-aligned report."""
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path

script=Path(__file__).resolve().parents[1]/"tools/compare_audio_taggers.py"
with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    for file_name, size in (("eff.csv",527),("yam.csv",521)):
        with (root/file_name).open("w",newline="",encoding="utf-8") as f:
            writer=csv.writer(f)
            writer.writerow(["index","mid","display_name"])
            for i in range(size):
                # Renamed label but shared MID: exact string must not determine match.
                writer.writerow([i,f"/m/id{i}",f"Yam class {i}" if file_name=="yam.csv" else f"Eff class {i}"])
    for name in ("eff.txt","yam.txt"):
        (root/name).touch()
    (root/"model.onnx").touch()
    for name,prefix in (("eff-bench","Eff"),("yam-bench","Yam")):
        file=root/name
        file.write_text("#!/usr/bin/env python3\n"
            "print('median_ms=12 p95_ms=14 min_ms=10 max_ms=16 rtf=0.012')\n"
            + "".join(f"print('{prefix} class {i} 0.{9-i}')\n" for i in range(5)),
            encoding="utf-8")
        file.chmod(0o755)
    with (root/"manifest.csv").open("w",newline="",encoding="utf-8") as f:
        writer=csv.writer(f)
        writer.writerow(["wav","mid"])
        writer.writerow(["first.wav","/m/id0|/m/id4"])
        writer.writerow(["not_common.wav","/m/id526"])
    args=[sys.executable,str(script),"--manifest",str(root/"manifest.csv"),
          "--mn-model",str(root/"model.onnx"),"--mn-labels",str(root/"eff.txt"),
          "--dymn-model",str(root/"model.onnx"),"--dymn-labels",str(root/"eff.txt"),
          "--yamnet-model",str(root/"model.onnx"),"--yamnet-labels",str(root/"yam.txt"),
          "--efficientat-csv",str(root/"eff.csv"),"--yamnet-csv",str(root/"yam.csv"),
          "--efficientat-benchmark",str(root/"eff-bench"),"--yamnet-benchmark",str(root/"yam-bench"),
          "--output",str(root/"result.json")]
    subprocess.run(args,check=True,capture_output=True,text=True)
    report=json.loads((root/"result.json").read_text())
    assert report["common_mids"]==521
    assert len(report["excluded"])==1
    for name in ("mn10_as","dymn10_as","yamnet"):
        model=report["models"][name]
        assert model["clips"]==1
        assert model["hit5"]==1
        assert model["macro_recall5"]==1
        assert model["mean_p95_ms"]==14
        assert model["mean_rtf"]==0.012
    print("PASS: three-model AudioSet MID alignment and excluded-label accounting")
