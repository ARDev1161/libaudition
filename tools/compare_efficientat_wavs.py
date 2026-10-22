#!/usr/bin/env python3
"""Compare two EfficientAT exports on identical annotated mono 32k WAV clips.

Does not conflate AudioSet 527-label results with YAMNet's 521-label ontology.
Input manifest: wav,label (multiple exact labels separated by '|').
Only first second of each WAV is evaluated by the benchmark binary.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess

def analyze(benchmark, model, labels, manifest_rows, root):
    vocabulary = set(labels.read_text(encoding="utf-8").splitlines())
    if len(vocabulary) != 527:
        raise ValueError(f"{labels}: expected 527 unique labels")
    observations=[]
    for row in manifest_rows:
        truth={x.strip() for x in row["label"].split("|") if x.strip()}
        missing=truth-vocabulary
        if missing:
            raise ValueError(f"Labels absent from vocabulary: {sorted(missing)}")
        clip=Path(row["wav"])
        if not clip.is_absolute():
            clip=root/clip
        raw=subprocess.run([str(benchmark),str(model),str(labels),str(clip)],
                           check=True,capture_output=True,text=True).stdout.splitlines()
        if len(raw)!=6 or not raw[0].startswith("median_ms="):
            raise ValueError(f"Malformed benchmark output for {clip}")
        latency=dict(field.split("=",1) for field in raw[0].split())
        predictions=[]
        for line in raw[1:]:
            label,score=line.rsplit(" ",1)
            predictions.append({"label":label,"score":float(score)})
        predicted={x["label"] for x in predictions}
        intersection=truth & predicted
        observations.append({
            "wav":str(clip),"truth":sorted(truth),"top5":predictions,
            "hit5":bool(intersection),"recall5":len(intersection)/len(truth),
            "median_ms":float(latency["median_ms"]),
            "p95_ms":float(latency["p95_ms"])})
    return {
        "clips":len(observations),
        "hit5":sum(x["hit5"] for x in observations)/len(observations),
        "macro_recall5":sum(x["recall5"] for x in observations)/len(observations),
        "mean_median_ms":sum(x["median_ms"] for x in observations)/len(observations),
        "observations":observations}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--manifest",type=Path,required=True)
    p.add_argument("--benchmark",type=Path,required=True)
    p.add_argument("--mn-model",type=Path,required=True)
    p.add_argument("--mn-labels",type=Path,required=True)
    p.add_argument("--dymn-model",type=Path,required=True)
    p.add_argument("--dymn-labels",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    args=p.parse_args()
    with args.manifest.open(newline="",encoding="utf-8") as f:
        rows=list(csv.DictReader(f))
    if not rows or not {"wav","label"}.issubset(rows[0]):
        p.error("manifest must contain wav,label and at least one row")
    output={
        "mn10_as":analyze(args.benchmark,args.mn_model.resolve(),args.mn_labels.resolve(),rows,args.manifest.resolve().parent),
        "dymn10_as":analyze(args.benchmark,args.dymn_model.resolve(),args.dymn_labels.resolve(),rows,args.manifest.resolve().parent)
    }
    args.output.write_text(json.dumps(output,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    for name,result in output.items():
        print(f"{name}: {result['clips']} clips hit@5={result['hit5']:.3f} "
              f"macro_recall@5={result['macro_recall5']:.3f} "
              f"mean_median_ms={result['mean_median_ms']:.2f}")
    print("These exploratory top-5 metrics are not AudioSet mAP.")

if __name__=="__main__":
    main()
