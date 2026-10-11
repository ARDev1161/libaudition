#!/usr/bin/env python3
"""Smoke test both-model comparison with a fake deterministic benchmark."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile

script=Path(__file__).resolve().parents[1]/"tools/compare_efficientat_wavs.py"
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    manifest=root/"manifest.csv"
    with manifest.open("w",newline="",encoding="utf-8") as f:
        w=csv.writer(f)
        w.writerow(["wav","label"])
        w.writerow(["test.wav","Speech|Dog"])
    benchmark=root/"fake_benchmark"
    benchmark.write_text(
        "#!/usr/bin/env python3\n"
        "import sys\n"
        "print('median_ms=4 p95_ms=5 min_ms=3 max_ms=6 rtf=0.004')\n"
        "print('Speech 0.9\\nNoise 0.7\\nMusic 0.6\\nBark 0.5\\nRain 0.3')\n",
        encoding="utf-8")
    benchmark.chmod(0o755)
    labels=root/"labels.txt"
    labels.write_text("\n".join(["Speech","Dog"]+[f"other{i}" for i in range(525)])+"\n",encoding="utf-8")
    model=root/"fake.onnx"
    model.touch()
    output=root/"result.json"
    result=subprocess.run([sys.executable,str(script),"--manifest",str(manifest),
        "--benchmark",str(benchmark),"--mn-model",str(model),"--mn-labels",str(labels),
        "--dymn-model",str(model),"--dymn-labels",str(labels),"--output",str(output)],
        check=True,capture_output=True,text=True)
    report=json.loads(output.read_text(encoding="utf-8"))
    for name in ("mn10_as","dymn10_as"):
        assert report[name]["hit5"]==1.0
        assert report[name]["macro_recall5"]==0.5
        assert report[name]["mean_median_ms"]==4.0
    assert "mn10_as" in result.stdout and "dymn10_as" in result.stdout
    print("PASS: two-model WAV comparison JSON and multilabel scores")
