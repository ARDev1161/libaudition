#!/usr/bin/env python3
"""Fail-closed corpus-level RTTM evaluation; optional real libaudition inference.

Reference RTTMs are always external and independent of the model prediction.
The primary DER is the micro-average across reference speech durations, NOT
the arithmetic mean of the individual recording DER values.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

from score_diarization_rttm import parse_rttm, score


def rttm_files(folder: Path) -> dict[str, Path]:
    if not folder.is_dir():
        raise ValueError(f"Missing RTTM directory: {folder}")
    items = sorted(folder.glob("*.rttm"))
    if not items:
        raise ValueError(f"No RTTM files in {folder}")
    return {path.stem: path for path in items}


def evaluate(
    reference_dir: Path,
    hypothesis_dir: Path,
    *,
    collar: float,
    skip_overlap: bool,
) -> dict:
    refs = rttm_files(reference_dir)
    hyps = rttm_files(hypothesis_dir)
    if refs.keys() != hyps.keys():
        missing = sorted(refs.keys() - hyps.keys())
        extra = sorted(hyps.keys() - refs.keys())
        raise ValueError(
            f"RTTM set mismatch (no partial scores): "
            f"missing hypotheses={missing}, extra hypotheses={extra}")
    rows = []
    for name in sorted(refs):
        rid, reference = parse_rttm(refs[name])
        hid, hypothesis = parse_rttm(hyps[name], allow_empty=True)
        if rid != name or (hid is not None and hid != name):
            raise ValueError(
                f"RTTM ID mismatch for {name}: reference={rid!r}, hypothesis={hid!r}")
        row = score(reference, hypothesis,
                    collar=collar, skip_overlap=skip_overlap)
        row["recording_id"] = name
        rows.append(row)
    sums = {
        key: sum(float(row[key]) for row in rows)
        for key in (
            "reference_speaker_seconds",
            "evaluated_seconds",
            "excluded_seconds",
            "missed_speaker_seconds",
            "false_alarm_speaker_seconds",
            "confusion_speaker_seconds",
        )
    }
    speech = sums["reference_speaker_seconds"]
    if not (speech > 0 and math.isfinite(speech)):
        raise ValueError("Corpus has no valid reference speech time")
    error = (sums["missed_speaker_seconds"] +
             sums["false_alarm_speaker_seconds"] +
             sums["confusion_speaker_seconds"])
    return {
        "schema": "libaudition.diarization-corpus-der.v1",
        "recordings": len(rows),
        "aggregation": "sum(missed+false_alarm+confusion)/sum(reference_speaker_seconds)",
        "collar_seconds": collar,
        "skip_overlap": skip_overlap,
        "corpus_der": round(error / speech, 9),
        "mean_recording_der_diagnostic_only":
            round(sum(row["der"] for row in rows) / len(rows), 9),
        "totals": {key: round(value, 9) for key, value in sums.items()},
        "per_recording": rows,
    }


def run_inference(
    reference_dir: Path,
    hypothesis_dir: Path,
    *,
    wav_dir: Path,
    diarizer: Path,
    segmentation_model: Path,
    embedding_model: Path,
) -> None:
    refs = rttm_files(reference_dir)
    if not wav_dir.is_dir() or not diarizer.is_file():
        raise ValueError("Missing WAV directory or compiled diarizer executable")
    for path in (segmentation_model, embedding_model):
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Missing real ONNX model weights: {path}")
    if not hypothesis_dir.exists():
        hypothesis_dir.mkdir(parents=True)
    if any(hypothesis_dir.iterdir()):
        raise ValueError("Prediction directory must be empty for a fresh run")
    for stem in sorted(refs):
        wav = wav_dir / f"{stem}.wav"
        if not wav.is_file() or wav.stat().st_size == 0:
            raise ValueError(f"Missing real 16-kHz mono WAV: {wav}")
    for stem in sorted(refs):
        wav = wav_dir / f"{stem}.wav"
        report = hypothesis_dir / f"{stem}.txt"
        command = [
            str(diarizer.resolve()),
            str(segmentation_model.resolve()),
            str(embedding_model.resolve()),
            str(wav.resolve()),
            str(report.resolve()),
            "--benchmark",
        ]
        print(f"Processing {stem} with real libaudition diarizer", flush=True)
        subprocess.run(command, check=True)
        rttm = hypothesis_dir / f"{stem}.rttm"
        if not rttm.is_file():
            raise RuntimeError(f"Diarizer did not write predicted RTTM: {rttm}")


def self_test() -> None:
    # Two independent reference recordings with very different durations.
    # First clip has DER=50% (1s missed out of 2); second is perfect on 8s.
    # Micro/corpus DER MUST be 1/10, not the 25% file mean.
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        ref = root / "reference"
        hyp = root / "hypothesis"
        ref.mkdir()
        hyp.mkdir()
        def emit(folder: Path, name: str, intervals: list[tuple[float, float, str]]):
            data = "".join(
                f"SPEAKER {name} 1 {start} {end-start} <NA> <NA> {speaker} <NA> <NA>\n"
                for start, end, speaker in intervals
            )
            (folder / f"{name}.rttm").write_text(data, encoding="utf-8")
        emit(ref, "a", [(0, 2, "A")])
        emit(hyp, "a", [(0, 1, "X")])
        emit(ref, "b", [(0, 8, "A")])
        emit(hyp, "b", [(0, 8, "Y")])
        result = evaluate(ref, hyp, collar=0, skip_overlap=False)
        assert result["recordings"] == 2
        assert abs(result["corpus_der"] - 0.1) < 1e-8, result
        assert abs(result["mean_recording_der_diagnostic_only"] - 0.25) < 1e-8
        assert result["totals"]["reference_speaker_seconds"] == 10
        emit(hyp, "unexpected", [(0, 1, "X")])
        try:
            evaluate(ref, hyp, collar=0, skip_overlap=False)
        except ValueError as exc:
            assert "extra hypotheses" in str(exc)
        else:
            raise AssertionError("Extra hypothesis recording silently ignored")
        (hyp / "unexpected.rttm").unlink()
        (hyp / "b.rttm").unlink()
        try:
            evaluate(ref, hyp, collar=0, skip_overlap=False)
        except ValueError as exc:
            assert "missing hypotheses" in str(exc)
        else:
            raise AssertionError("Missing hypothesis recording silently ignored")
        (hyp / "b.rttm").write_text(
            "SPEAKER unexpected-id 1 0 8 <NA> <NA> Y <NA> <NA>\n",
            encoding="utf-8")
        try:
            evaluate(ref, hyp, collar=0, skip_overlap=False)
        except ValueError as exc:
            assert "RTTM ID mismatch" in str(exc)
        else:
            raise AssertionError("Incorrect RTTM recording ID silently accepted")
    print("diarization-corpus-DER-scoring-self-test=ok")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path)
    parser.add_argument("--hypothesis-dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--collar", type=float, default=0.0)
    parser.add_argument("--skip-overlap", action="store_true")
    parser.add_argument("--max-der", type=float)
    parser.add_argument("--wav-dir", type=Path)
    parser.add_argument("--diarizer", type=Path)
    parser.add_argument("--segmentation-model", type=Path)
    parser.add_argument("--embedding-model", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not all((args.reference_dir, args.hypothesis_dir, args.output)):
        parser.error("--reference-dir, --hypothesis-dir and --output required")
    if not math.isfinite(args.collar) or args.collar < 0:
        parser.error("--collar must be finite and nonnegative")
    if args.max_der is not None and (
            not math.isfinite(args.max_der) or args.max_der < 0):
        parser.error("--max-der must be finite and nonnegative")
    inference = [args.wav_dir, args.diarizer, args.segmentation_model,
                 args.embedding_model]
    if any(x is not None for x in inference):
        if not all(x is not None for x in inference):
            parser.error("Inference requires --wav-dir, --diarizer, "
                         "--segmentation-model and --embedding-model together")
        run_inference(args.reference_dir, args.hypothesis_dir,
                      wav_dir=args.wav_dir, diarizer=args.diarizer,
                      segmentation_model=args.segmentation_model,
                      embedding_model=args.embedding_model)
    report = evaluate(args.reference_dir, args.hypothesis_dir,
                      collar=args.collar, skip_overlap=args.skip_overlap)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    print(json.dumps({key: report[key] for key in (
        "recordings", "corpus_der", "mean_recording_der_diagnostic_only",
        "totals")}, sort_keys=True))
    if args.max_der is not None and report["corpus_der"] > args.max_der:
        raise AssertionError(
            f"Corpus DER {report['corpus_der']} > calibrated {args.max_der}")


if __name__ == "__main__":
    main()
