#!/usr/bin/env python3
"""Self-test for manifest scorer without requiring weights or real WAV assets."""
import csv
import pathlib
import subprocess
import sys
import tempfile

scorer=pathlib.Path(__file__).resolve().parents[1]/"tools"/"evaluate_efficientat_wavs.py"
with tempfile.TemporaryDirectory() as d:
    p=pathlib.Path(d)
    manifest=p/"manifest.csv"
    with manifest.open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=["wav","label"])
        w.writeheader()
        w.writerow({"wav":"one.wav","label":"Speech|Dog"})
        w.writerow({"wav":"two.wav","label":"Bell"})
    executable=p/"fake_benchmark"
    executable.write_text(
        "#!/usr/bin/env python3\n"
        "import sys\n"
        "print('median_ms=1 p95_ms=1 min_ms=1 max_ms=1 rtf=0.001')\n"
        "if sys.argv[-1].endswith('one.wav'):\n"
        " print('Speech 0.9\\nMusic 0.8\\nDog 0.7\\nNoise 0.6\\nSpeech synthesizer 0.5')\n"
        "else:\n"
        " print('Noise 0.9\\nMusic 0.8\\nSpeech 0.7\\nWind 0.6\\nRain 0.5')\n",
        encoding="utf-8")
    executable.chmod(0o755)
    (p/"model.onnx").touch()
    (p/"labels.txt").touch()
    result=subprocess.run([sys.executable,str(scorer),
        "--manifest",str(manifest),"--model",str(p/"model.onnx"),
        "--labels",str(p/"labels.txt"),"--benchmark",str(executable)],
        capture_output=True,text=True,check=True)
    assert "Clips=2 hit@5=0.5000 macro_recall@5=0.5000" in result.stdout, result.stdout
    print("PASS: scorer manifest, multi-label recall and negative clips")
