#!/usr/bin/env python3
"""Audit AudioSet label compatibility between EfficientAT 527 and YAMNet 521.

Both CSV maps contain index,mid,display_name. MID is the stable AudioSet
identifier. Do not align classifier outputs by position or display name.
"""
import argparse
import csv
import json
from pathlib import Path

def load(path, expected):
    with Path(path).open(newline="", encoding="utf-8-sig") as handle:
        rows=list(csv.DictReader(handle))
    if len(rows)!=expected or any(not r.get("mid") or not r.get("display_name") for r in rows):
        raise ValueError(f"{path}: expected {expected} CSV classes with mid and display_name")
    result={}
    for row in rows:
        mid=row["mid"].strip()
        if mid in result:
            raise ValueError(f"{path}: duplicated MID {mid}")
        result[mid]=row["display_name"].strip()
    return result

def report(efficientat, yamnet):
    common=efficientat.keys() & yamnet.keys()
    return {
        "efficientat_classes":len(efficientat),
        "yamnet_classes":len(yamnet),
        "common_mids":len(common),
        "efficientat_only":[{"mid":m,"label":efficientat[m]} for m in sorted(efficientat.keys()-yamnet.keys())],
        "yamnet_only":[{"mid":m,"label":yamnet[m]} for m in sorted(yamnet.keys()-efficientat.keys())],
        "renamed_common":[{"mid":m,"efficientat":efficientat[m],"yamnet":yamnet[m]}
                          for m in sorted(common) if efficientat[m]!=yamnet[m]],
    }

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--efficientat-csv",type=Path,required=True)
    p.add_argument("--yamnet-csv",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    args=p.parse_args()
    data=report(load(args.efficientat_csv,527),load(args.yamnet_csv,521))
    args.output.write_text(json.dumps(data,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    print(f"AudioSet common MIDs: {data['common_mids']}; "
          f"EfficientAT-only: {len(data['efficientat_only'])}; "
          f"YAMNet-only: {len(data['yamnet_only'])}; "
          f"renamed: {len(data['renamed_common'])}")
if __name__=="__main__":
    main()
