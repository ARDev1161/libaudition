#!/usr/bin/env python3
"""Review an existing three-model ESC-10 JSON comparison for invalid signals.

This intentionally does not claim to measure generalization on tiny,
clip-level-labeled, one-second excerpts.
"""
import argparse
import json
from pathlib import Path

def analyze(report):
    findings=[]
    models=report["models"]
    for name,model in models.items():
        obs=model["observations"]
        for row in obs:
            saturated=[entry["label"] for entry in row["top5"] if entry["score"]>=0.9999]
            if len(saturated)>=3:
                findings.append({"model":name,"wav":row["wav"],"kind":"saturated_top5",
                                 "detail":saturated})
            if row["top5"] and row["top5"][0]["label"]=="Silence" and row["top5"][0]["score"]>=0.9:
                findings.append({"model":name,"wav":row["wav"],"kind":"silence_dominant",
                                 "detail":row["top5"][0]["score"]})
        if len(obs)<30:
            findings.append({"model":name,"kind":"insufficient_corpus","detail":len(obs)})
    return findings

def main():
    p=argparse.ArgumentParser()
    p.add_argument("report",type=Path)
    p.add_argument("--output",type=Path)
    args=p.parse_args()
    report=json.loads(args.report.read_text(encoding="utf-8"))
    findings=analyze(report)
    for f in findings:
        print(f"{f['model']}: {f['kind']} ({f['detail']}) on {f.get('wav','corpus')}")
    if args.output:
        args.output.write_text(json.dumps(findings,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    print("Diagnostic only. No validity or accuracy claims.")

if __name__=="__main__":
    main()
