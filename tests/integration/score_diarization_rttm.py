#!/usr/bin/env python3
"""Reference-only RTTM diarization error rate (DER), with exact interval arithmetic.

No reference RTTM is bundled for sherpa-onnx's example two-speaker WAV.
This tool MUST NOT treat model predictions or inferred turn boundaries as truth.
Supports scored speaker overlap, optimal global label assignment, boundary
collars and explicit overlap exclusion; no third-party Python dependencies.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path


@dataclass(frozen=True)
class Segment:
    start: float
    end: float
    speaker: str


def parse_rttm(path: Path, *, allow_empty: bool = False) -> tuple[str | None, list[Segment]]:
    """Strict single-recording RTTM reader; never silently skip bad records."""
    lines = path.read_text(encoding="utf-8").splitlines()
    recording = None
    segments = []
    for number, line in enumerate(lines, 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        cols = line.split()
        if len(cols) != 10 or cols[0] != "SPEAKER":
            raise ValueError(f"{path}:{number}: expected 10-field SPEAKER RTTM")
        if cols[2] != "1":
            raise ValueError(f"{path}:{number}: only channel 1 is supported")
        if recording is None:
            recording = cols[1]
        elif recording != cols[1]:
            raise ValueError(f"{path}:{number}: multiple recording IDs are not supported")
        try:
            start, duration = float(cols[3]), float(cols[4])
        except ValueError as exc:
            raise ValueError(f"{path}:{number}: invalid segment time") from exc
        if not (math.isfinite(start) and start >= 0 and
                math.isfinite(duration) and duration > 0 and
                math.isfinite(start + duration)):
            raise ValueError(f"{path}:{number}: invalid start/duration")
        speaker = cols[7]
        if not speaker or speaker == "<NA>":
            raise ValueError(f"{path}:{number}: missing speaker ID")
        segments.append(Segment(start, start + duration, speaker))
    if not segments and not allow_empty:
        raise ValueError(f"{path}: missing annotated speaker segments")
    return recording, segments


def max_matching_seconds(weights: list[list[float]]) -> tuple[float, list[tuple[int, int]]]:
    """Maximum-weight injective ref-to-hyp speaker mapping, Hungarian O(n^3)."""
    nr = len(weights)
    nc = len(weights[0]) if nr else 0
    n = max(nr, nc)
    if n == 0:
        return 0.0, []
    if any(len(row) != nc for row in weights):
        raise ValueError("Ragged matching matrix")
    maximum = max((w for row in weights for w in row), default=0.0)
    cost = [[maximum - (weights[i][j] if i < nr and j < nc else 0.0)
             for j in range(n)] for i in range(n)]
    u = [0.0] * (n + 1)
    v = [0.0] * (n + 1)
    p = [0] * (n + 1)
    way = [0] * (n + 1)
    for i in range(1, n + 1):
        p[0] = i
        minv = [math.inf] * (n + 1)
        used = [False] * (n + 1)
        j0 = 0
        while True:
            used[j0] = True
            i0 = p[j0]
            delta = math.inf
            j1 = 0
            for j in range(1, n + 1):
                if used[j]:
                    continue
                cur = cost[i0 - 1][j - 1] - u[i0] - v[j]
                if cur < minv[j]:
                    minv[j] = cur
                    way[j] = j0
                if minv[j] < delta:
                    delta, j1 = minv[j], j
            for j in range(n + 1):
                if used[j]:
                    u[p[j]] += delta
                    v[j] -= delta
                else:
                    minv[j] -= delta
            j0 = j1
            if p[j0] == 0:
                break
        while True:
            j1 = way[j0]
            p[j0] = p[j1]
            j0 = j1
            if j0 == 0:
                break
    pairs = [(p[j] - 1, j - 1) for j in range(1, n + 1)
             if 1 <= p[j] <= nr and j <= nc]
    return sum(weights[i][j] for i, j in pairs), pairs


def score(reference: list[Segment], hypothesis: list[Segment], *,
          collar: float = 0.0, skip_overlap: bool = False) -> dict:
    """Exact DER on atomic intervals (speaker permutation invariant).

    Reference speech seconds include simultaneous speakers separately.
    A full-width collar masks +/- collar/2 around each reference boundary.
    """
    if not reference:
        raise ValueError("Reference speaker annotation cannot be empty")
    if not math.isfinite(collar) or collar < 0:
        raise ValueError("Collar must be a finite nonnegative number")
    for s in reference + hypothesis:
        if not (math.isfinite(s.start) and math.isfinite(s.end) and
                0 <= s.start < s.end and s.speaker):
            raise ValueError("Invalid RTTM segment")
    ref_ids = sorted({s.speaker for s in reference})
    hyp_ids = sorted({s.speaker for s in hypothesis})
    ref_index = {name: i for i, name in enumerate(ref_ids)}
    hyp_index = {name: i for i, name in enumerate(hyp_ids)}
    bounds = {p for seg in reference + hypothesis for p in (seg.start, seg.end)}
    collars = []
    for seg in reference:
        for t in (seg.start, seg.end):
            if collar > 0:
                lo, hi = max(0.0, t - collar / 2), t + collar / 2
                bounds.update((lo, hi))
                collars.append((lo, hi))
    bounds = sorted(bounds)
    weights = [[0.0 for _ in hyp_ids] for _ in ref_ids]
    ref_seconds = missed = false_alarm = minimum_confusion = excluded = 0.0
    evaluated_seconds = 0.0
    for left, right in zip(bounds, bounds[1:]):
        dt = right - left
        if dt <= 0:
            continue
        middle = (left + right) / 2
        r = {s.speaker for s in reference if s.start <= middle < s.end}
        h = {s.speaker for s in hypothesis if s.start <= middle < s.end}
        if (skip_overlap and len(r) > 1) or any(a <= middle < b for a, b in collars):
            excluded += dt
            continue
        evaluated_seconds += dt
        ref_seconds += len(r) * dt
        missed += max(0, len(r) - len(h)) * dt
        false_alarm += max(0, len(h) - len(r)) * dt
        minimum_confusion += min(len(r), len(h)) * dt
        for ri in r:
            for hi in h:
                weights[ref_index[ri]][hyp_index[hi]] += dt
    if ref_seconds <= 0:
        raise ValueError("No scorable reference speech (check collar and overlap)")
    matched, pairs = max_matching_seconds(weights)
    confusion = max(0.0, minimum_confusion - matched)
    error = missed + false_alarm + confusion
    return {
        "schema": "libaudition.diarization-der.v1",
        "reference_speakers": len(ref_ids),
        "hypothesis_speakers": len(hyp_ids),
        "speaker_mapping": {hyp_ids[j]: ref_ids[i] for i, j in pairs
                            if weights[i][j] > 0},
        "reference_speaker_seconds": round(ref_seconds, 9),
        "evaluated_seconds": round(evaluated_seconds, 9),
        "excluded_seconds": round(excluded, 9),
        "missed_speaker_seconds": round(missed, 9),
        "false_alarm_speaker_seconds": round(false_alarm, 9),
        "confusion_speaker_seconds": round(confusion, 9),
        "der": round(error / ref_seconds, 9),
        "collar_seconds": collar,
        "skip_overlap": skip_overlap,
    }


def self_test() -> None:
    def seg(a, b, sp):
        return Segment(a, b, sp)
    def approx(actual, expected):
        assert abs(actual - expected) < 1e-8, (actual, expected)

    ref = [seg(0, 2, "A"), seg(2, 4, "B")]
    same = [seg(0, 2, "X"), seg(2, 4, "Y")]
    swapped = [seg(0, 2, "Y"), seg(2, 4, "X")]
    approx(score(ref, same)["der"], 0.0)
    approx(score(ref, swapped)["der"], 0.0)
    approx(score([seg(0, 2, "A")], [seg(0, 1, "X")])["der"], 0.5)
    approx(score([seg(0, 2, "A")], [seg(0, 3, "X")])["der"], 0.5)
    mixed = score(ref, [seg(0, 4, "X")])
    approx(mixed["der"], 0.5)
    approx(mixed["confusion_speaker_seconds"], 2.0)
    overlap_ref = [seg(0, 2, "A"), seg(1, 3, "B")]
    overlap_hyp = [seg(0, 2, "X"), seg(1, 3, "Y")]
    approx(score(overlap_ref, overlap_hyp)["der"], 0.0)
    approx(score(overlap_ref, overlap_hyp)["reference_speaker_seconds"], 4.0)
    approx(score(overlap_ref, overlap_hyp, skip_overlap=True)
           ["reference_speaker_seconds"], 2.0)
    approx(score([seg(0, 2, "A")], [seg(0, 1.95, "X")],
                 collar=0.2)["der"], 0.0)
    assert score(ref, same)["speaker_mapping"] == {"X": "A", "Y": "B"}
    assert score(ref, [])["der"] == 1.0
    # Round-trip strict RTTM parsing and forbid using an empty reference.
    from tempfile import TemporaryDirectory
    with TemporaryDirectory() as directory:
        file = Path(directory) / "sample.rttm"
        file.write_text(
            "SPEAKER rec 1 0.000 2.000 <NA> <NA> spk_0 <NA> <NA>\\n"
            "SPEAKER rec 1 2.000 2.000 <NA> <NA> spk_1 <NA> <NA>\\n",
            encoding="utf-8")
        recording, parsed = parse_rttm(file)
        assert recording == "rec" and parsed == [
            seg(0, 2, "spk_0"), seg(2, 4, "spk_1")]
        file.write_text("SPEAKER rec 1 NaN 2 <NA> <NA> x <NA> <NA>\\n",
                        encoding="utf-8")
        try:
            parse_rttm(file)
        except ValueError:
            pass
        else:
            raise AssertionError("Non-finite RTTM accepted")

    for bad in [[], [seg(1, 1, "X")]]:
        try:
            score(bad, same)
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid reference accepted")
    print("diarization-DER-scoring-self-test=ok")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--reference", type=Path, help="Independently annotated RTTM, mandatory")
    p.add_argument("--hypothesis", type=Path, help="Model output RTTM")
    p.add_argument("--output", type=Path, help="JSON metrics")
    p.add_argument("--collar", type=float, default=0.0,
                   help="Full collar width in seconds around reference boundaries")
    p.add_argument("--skip-overlap", action="store_true")
    p.add_argument("--max-der", type=float, help="Optional acceptance threshold; may exceed 1")
    p.add_argument("--validate-hypothesis", type=Path,
                   help="Check predicted RTTM format only; does NOT calculate DER")
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        self_test()
        return
    if args.validate_hypothesis is not None:
        if (args.reference is not None or args.hypothesis is not None or
                args.output is not None or args.max_der is not None):
            p.error("--validate-hypothesis cannot be combined with DER evaluation")
        recording, segments = parse_rttm(args.validate_hypothesis)
        print(json.dumps({"recording_id": recording,
                          "hypothesis_segments": len(segments),
                          "metric": None,
                          "reason": "No independently annotated reference provided"},
                         sort_keys=True))
        return
    if args.reference is None or args.hypothesis is None or args.output is None:
        p.error("--reference --hypothesis --output required; cannot score without ground truth")
    ref_id, reference = parse_rttm(args.reference)
    hyp_id, hypothesis = parse_rttm(args.hypothesis, allow_empty=True)
    if hyp_id is not None and ref_id != hyp_id:
        raise ValueError(f"RTTM recording mismatch: {ref_id!r} != {hyp_id!r}")
    metrics = score(reference, hypothesis, collar=args.collar,
                    skip_overlap=args.skip_overlap)
    metrics["recording_id"] = ref_id
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    print(json.dumps(metrics, sort_keys=True))
    if args.max_der is not None:
        if not math.isfinite(args.max_der) or args.max_der < 0:
            raise ValueError("--max-der must be finite and >= 0")
        if metrics["der"] > args.max_der:
            raise AssertionError(
                f"DER {metrics['der']} exceeds calibrated limit {args.max_der}")


if __name__ == "__main__":
    main()
