#!/usr/bin/env python3
"""Offline deterministic YAMNet event-timeline tests; no model download."""
import csv
import importlib.util
import tempfile
from pathlib import Path

import numpy as np

module_path = Path(__file__).resolve().parents[1] / "tools/analyze_yamnet_event_timeline.py"
spec = importlib.util.spec_from_file_location("yamnet_event_timeline", module_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

labels = [f"class_{i}" for i in range(521)]
# A brief dog event falls in a single patch surrounded by silence.
# Clip-wide mean dilutes it, while the frame-level maximum preserves its onset.
scores = np.zeros((3, 521), dtype=np.float32)
scores[:, 0] = 0.90
scores[1, 1] = 0.99
out = module.summarise_scores(scores, labels, [1])
assert out["patches"] == 3
assert out["mean_top5"][0]["label"] == "class_0"
assert out["peak_top5"][0]["label"] == "class_1"
assert out["ground_truth"][0]["peak_patch"] == 1
assert out["ground_truth"][0]["approx_peak_start_s"] == 0.48
assert out["ground_truth"][0]["peak_rank"] == 1
assert abs(out["ground_truth"][0]["mean_score"] - 0.33) < 1e-5

# Reject malformed probabilities before reporting rankings.
for bad in (np.zeros((0, 521)), np.ones((3, 520)),
            np.full((3, 521), np.nan),
            np.full((3, 521), 1.01)):
    try:
        module.summarise_scores(bad, labels, [1])
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid score matrix accepted")

with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / "map.csv"
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["index", "mid", "display_name"])
        for i in range(521):
            writer.writerow([i, f"/m/{i}", labels[i]])
    mids, names = module.parse_class_map(path)
    assert len(mids) == 521 and names == labels
    original = path.read_text(encoding="utf-8")
    path.write_text(original.replace("10,/m/10,class_10", "11,/m/10,class_10"),
                    encoding="utf-8")
    try:
        module.parse_class_map(path)
    except ValueError:
        pass
    else:
        raise AssertionError("Out-of-order AudioSet class map accepted")

print("PASS: YAMNet peak/mean timeline, frame location, score and ontology validation")
