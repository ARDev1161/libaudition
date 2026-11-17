#!/usr/bin/env python3
"""Fail-closed one-to-one azimuth scorer for experimental Acoustix SSL windows.

Input oracle JSON: {"windows":[{"start_sample":0,"end_sample":8000,
"azimuths_deg":[-45,30]}, ...]}. Hypotheses use probe's windows schema.
This is SSL-only: never interpret matches as tracks or separated audio.
"""
import argparse
import json
import math


def distance(a, b):
    return abs((a - b + 180) % 360 - 180)


def _angle(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("invalid numeric azimuth")
    return float(value)


def _bounds(window):
    start, end = window["start_sample"], window["end_sample"]
    if (type(start) is not int or type(end) is not int
            or start < 0 or end <= start):
        raise ValueError("invalid window sample bounds")
    return start, end


def score(hypotheses, oracle, tolerance_deg=25):
    if (isinstance(tolerance_deg, bool) or not isinstance(tolerance_deg, (int, float))
            or not math.isfinite(tolerance_deg) or not 0 < tolerance_deg <= 180):
        raise ValueError("invalid angular tolerance")
    if not isinstance(hypotheses, list) or not isinstance(oracle, list):
        raise ValueError("hypotheses and oracle windows must be lists")
    if not hypotheses or not oracle:
        raise ValueError("empty SSL window comparison is not evidence")
    if len(hypotheses) != len(oracle):
        raise ValueError("window count mismatch")
    tp = fp = fn = 0
    errors = []
    previous_start = -1
    for h, truth in zip(hypotheses, oracle):
        hb, tb = _bounds(h), _bounds(truth)
        if hb[0] <= previous_start:
            raise ValueError("duplicate or unordered window starts")
        previous_start = hb[0]
        if hb != tb:
            raise ValueError("window sample bounds mismatch")
        targets = truth["azimuths_deg"]
        if not isinstance(targets, list):
            raise ValueError("oracle azimuths must be a list")
        targets = [_angle(x) for x in targets]
        if h["status"] not in ("silent", "hypothesis"):
            raise ValueError("unknown hypothesis status")
        if h["status"] == "silent" and h.get("peak_azimuth_deg") is not None:
            raise ValueError("silent window must not contain a direction")
        detected = [] if h["status"] == "silent" else [h["peak_azimuth_deg"]]
        detected = [_angle(x) for x in detected]
        # One-to-one minimum-distance matching, never reuse a detection.
        pairs = sorted((distance(a, b), i, j)
                       for i, a in enumerate(detected)
                       for j, b in enumerate(targets))
        matched_i, matched_j = set(), set()
        for err, i, j in pairs:
            if err <= tolerance_deg and i not in matched_i and j not in matched_j:
                matched_i.add(i)
                matched_j.add(j)
                errors.append(err)
        tp += len(matched_i)
        fp += len(detected) - len(matched_i)
        fn += len(targets) - len(matched_j)
    def percentile(p):
        if not errors:
            return None
        values = sorted(errors)
        pos = (len(values) - 1) * p
        lo = int(math.floor(pos))
        hi = int(math.ceil(pos))
        return values[lo] + (values[hi] - values[lo]) * (pos - lo)
    return {"true_positives": tp, "false_proposals": fp, "missed_sources": fn,
            "precision": tp / (tp + fp) if tp + fp else None,
            "recall": tp / (tp + fn) if tp + fn else None,
            "matched_azimuth_median_deg": percentile(.5),
            "matched_azimuth_p90_deg": percentile(.9),
            "status": "diagnostic_ssl_only"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hypotheses", required=True)
    parser.add_argument("--oracle", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    with open(args.hypotheses, encoding="utf-8") as f:
        hypotheses = json.load(f)
    with open(args.oracle, encoding="utf-8") as f:
        oracle = json.load(f)
    result = score(hypotheses["windows"], oracle["windows"])
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)


if __name__ == "__main__":
    main()
