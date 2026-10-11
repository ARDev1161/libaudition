#!/usr/bin/env python3
"""Evaluate EfficientAT on user-provided mono 32 kHz PCM16 WAV clips.

CSV: wav,label (pipe-separated exact AudioSet display names for multi-label).
Reports hit@5 and macro recall@5, NOT AudioSet mAP. Each WAV's first second
is evaluated by the C++ benchmark. No copyrighted fixtures downloaded.
"""
import argparse
import csv
from pathlib import Path
import subprocess

p=argparse.ArgumentParser()
p.add_argument("--manifest",type=Path,required=True)
p.add_argument("--model",type=Path,required=True)
p.add_argument("--labels",type=Path,required=True)
p.add_argument("--benchmark",type=Path,required=True)
args=p.parse_args()
with args.manifest.open(newline="",encoding="utf-8") as f:
    rows=list(csv.DictReader(f))
if not rows or not {"wav","label"}.issubset(rows[0]):
    raise SystemExit("manifest must have non-empty rows and wav,label columns")
hits=0
recall=0.0
for idx,row in enumerate(rows,1):
    wav=Path(row["wav"])
    if not wav.is_absolute():
        wav=args.manifest.parent/wav
    truth={s.strip() for s in row["label"].split("|") if s.strip()}
    if not truth:
        raise SystemExit(f"row {idx}: empty labels")
    r=subprocess.run([str(args.benchmark.resolve()),str(args.model.resolve()),
                      str(args.labels.resolve()),str(wav.resolve())],
                     check=True,capture_output=True,text=True)
    lines=r.stdout.splitlines()
    if len(lines)<6 or not lines[0].startswith("median_ms="):
        raise SystemExit(f"row {idx}: malformed classifier output: {r.stdout}")
    predicted={line.rsplit(" ",1)[0] for line in lines[1:6]}
    matches=truth&predicted
    hits+=bool(matches)
    recall+=len(matches)/len(truth)
    print(f"{wav.name}: hit={bool(matches)} expected={sorted(truth)} predicted={sorted(predicted)}")
print(f"Clips={len(rows)} hit@5={hits/len(rows):.4f} macro_recall@5={recall/len(rows):.4f}")
print("Only the first 1 s of every WAV is classified. This is not mAP.")
