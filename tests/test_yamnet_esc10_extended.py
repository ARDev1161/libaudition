#!/usr/bin/env python3
"""Offline regressions for ESC-10 sampling and balanced YAMNet evaluation."""
import importlib.util
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1] / "tools"


def load(module_name):
    spec = importlib.util.spec_from_file_location(module_name, ROOT / (module_name + ".py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


prep = load("prepare_esc10_evaluation")
evaluator = load("evaluate_yamnet_esc10")
rows = [{"category": category, "esc10": "True", "fold": str(fold),
         "filename": f"{category}-{fold}-{i:02d}.wav"}
        for category in prep.CATEGORIES for fold in range(1, 6) for i in range(8)]
selected = prep.choose_examples(rows, 20)
assert list(selected) == sorted(prep.CATEGORIES)
assert all(len(examples) == 20 for examples in selected.values())
for examples in selected.values():
    assert len({r["filename"] for r in examples}) == 20
    assert {fold: sum(r["fold"] == str(fold) for r in examples)
            for fold in range(1, 6)} == {fold: 4 for fold in range(1, 6)}
small = prep.choose_examples(rows, 1)
assert all(v[0] == next(r for r in rows if r["category"] == c)
           for c, v in small.items())
for invalid in (0, 41):
    try:
        prep.choose_examples(rows, invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid corpus size accepted")
try:
    prep.choose_examples(rows[:3], 20)
except ValueError:
    pass
else:
    raise AssertionError("Insufficient ESC-10 examples accepted")

scores = np.zeros((4, 521), dtype=np.float32)
scores[:, 0] = 0.7
scores[1, 1] = 0.99
ranks, values = evaluator.compute_ranks(scores)
assert int(ranks["mean"][0]) == 0
assert int(ranks["peak"][0]) == 1
assert abs(values["mean"][1] - 0.2475) < 1e-6
for bad in (np.zeros((0, 521)), np.ones((1, 520)),
            np.full((1, 521), np.nan), np.full((1, 521), 1.01)):
    try:
        evaluator.compute_ranks(bad)
    except ValueError:
        pass
    else:
        raise AssertionError("Malformed ONNX scores accepted")


def observation(mid, first, mean, peak):
    return {"truth_mid": mid,
            "first_second": {"top1_hit": first, "top5_hit": first},
            "whole_clip_mean": {"top1_hit": mean, "top5_hit": mean},
            "whole_clip_peak": {"top1_hit": peak, "top5_hit": peak}}


reports = [observation("/m/a", True, True, True) for _ in range(3)] + [
    observation("/m/b", False, False, True)]
metrics = evaluator.summarize_records(reports, ["/m/a", "/m/b"])
assert metrics["whole_clip_mean"]["top5_micro"] == 0.75
assert metrics["whole_clip_mean"]["top5_macro"] == 0.5
assert metrics["whole_clip_peak"]["top1_macro"] == 1.0
assert metrics["whole_clip_mean"]["per_class"]["/m/b"]["clips"] == 1
try:
    evaluator.summarize_records(reports, ["/m/a", "/m/b", "/m/c"])
except ValueError:
    pass
else:
    raise AssertionError("Missing-class macro evaluation accepted")
print("PASS: ESC-10 fold selection and YAMNet balanced 1s/5s scoring")
