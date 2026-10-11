#!/usr/bin/env python3
"""Compare YAMNet, MN10 and DyMN10 on the identical annotated 32-kHz WAVs.

Ground truth CSV: wav,mid (multiple AudioSet MIDs separated by '|').
All metrics are restricted to MIDs present in BOTH model vocabularies.
MN10 consumes the first 5s, while DyMN10/YAMNet consume the first 1s; latency includes
classifier execution but excludes CLI startup, model loading and WAV decoding.
"""
import argparse
import csv
import json
import math
import subprocess
from pathlib import Path

def vocabulary(path, count):
    with path.open(newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if len(rows) != count:
        raise ValueError(f"{path}: expected {count} labels, got {len(rows)}")
    by_mid, by_name = {}, {}
    for row in rows:
        mid, name = row["mid"].strip(), row["display_name"].strip()
        if not mid or not name or mid in by_mid or name in by_name:
            raise ValueError(f"{path}: empty or ambiguous label/MID")
        by_mid[mid] = name
        by_name[name] = mid
    return by_mid, by_name

def run(binary, model, labels, wav, name_to_mid):
    text = subprocess.run([str(binary), str(model), str(labels), str(wav)],
                          check=True, capture_output=True, text=True).stdout.splitlines()
    if len(text) != 6 or not text[0].startswith("median_ms="):
        raise ValueError(f"Unexpected benchmark output: {text}")
    times = dict(item.split("=", 1) for item in text[0].split())
    result = []
    for line in text[1:]:
        name, number = line.rsplit(" ", 1)
        if name not in name_to_mid:
            raise ValueError(f"Prediction absent from model CSV: {name}")
        score = float(number)
        if not math.isfinite(score):
            raise ValueError("Non-finite score")
        result.append({"mid": name_to_mid[name], "label": name, "score": score})
    return result, {k: float(times[k]) for k in ("median_ms", "p95_ms", "rtf")}

def main():
    p = argparse.ArgumentParser()
    for flag in ("manifest", "mn-model", "mn-labels", "dymn-model", "dymn-labels",
                 "yamnet-model", "yamnet-labels", "efficientat-csv", "yamnet-csv",
                 "efficientat-benchmark", "yamnet-benchmark", "output"):
        p.add_argument("--" + flag, type=Path, required=True)
    a = p.parse_args()
    eff, eff_names = vocabulary(a.efficientat_csv, 527)
    yam, yam_names = vocabulary(a.yamnet_csv, 521)
    common = set(eff) & set(yam)
    with a.manifest.open(newline="", encoding="utf-8") as f:
        manifest = list(csv.DictReader(f))
    if not manifest or not {"wav", "mid"}.issubset(manifest[0]):
        p.error("Expected nonempty manifest with wav,mid columns")
    configs = {
        "mn10_as": (a.efficientat_benchmark, a.mn_model, a.mn_labels, eff_names),
        "dymn10_as": (a.efficientat_benchmark, a.dymn_model, a.dymn_labels, eff_names),
        "yamnet": (a.yamnet_benchmark, a.yamnet_model, a.yamnet_labels, yam_names),
    }
    observations = {key: [] for key in configs}
    excluded = []
    for row in manifest:
        truth = {s.strip() for s in row["mid"].split("|") if s.strip()}
        if not truth or not truth <= (set(eff) | set(yam)):
            raise ValueError(f"Unrecognized or empty ground truth: {row}")
        if not truth <= common:
            excluded.append({"wav": row["wav"], "unsupported_mids": sorted(truth - common)})
            continue
        wav = Path(row["wav"])
        if not wav.is_absolute():
            wav = a.manifest.resolve().parent / wav
        for name, (binary, model, labels, mapping) in configs.items():
            predictions, timing = run(binary.resolve(), model.resolve(), labels.resolve(), wav.resolve(), mapping)
            # Predictions outside the shared ontology count as occupying one of
            # five slots, but cannot become false positive shared-label matches.
            predicted = {r["mid"] for r in predictions}
            overlap = truth & predicted
            observations[name].append({
                "wav": row["wav"], "truth_mids": sorted(truth), "top5": predictions,
                "hit5": bool(overlap), "recall5": len(overlap) / len(truth), **timing,
            })
    if not observations["yamnet"]:
        raise ValueError("No shared-ontology clips available for comparison")
    result = {
        "window_seconds": {"mn10_as": 5, "dymn10_as": 1, "yamnet": 1},
        "common_mids": len(common), "excluded": excluded,
        "models": {
            name: {
                "clips": len(obs),
                "hit5": sum(x["hit5"] for x in obs) / len(obs),
                "macro_recall5": sum(x["recall5"] for x in obs) / len(obs),
                "mean_median_ms": sum(x["median_ms"] for x in obs) / len(obs),
                "mean_p95_ms": sum(x["p95_ms"] for x in obs) / len(obs),
                "mean_rtf": sum(x["rtf"] for x in obs) / len(obs),
                "observations": obs
            } for name, obs in observations.items()
        }
    }
    a.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    for name, metrics in result["models"].items():
        print(f"{name}: n={metrics['clips']} hit@5={metrics['hit5']:.3f} "
              f"recall@5={metrics['macro_recall5']:.3f} "
              f"median_ms(mean)={metrics['mean_median_ms']:.2f}")
    print(f"Excluded {len(excluded)} non-shared-ontology clips; these metrics are NOT AudioSet mAP.")

if __name__ == "__main__":
    main()
