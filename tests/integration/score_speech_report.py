#!/usr/bin/env python3
"""Independent ASR quality report: normalized WER/CER, optional strict gates.

This is never silently a PASS on an empty or missing result; WER/CER gates
are opt-in until a reference baseline for this exact fixture is accepted.
"""
import argparse
import json
import re
import unicodedata
from pathlib import Path


def normalized(text):
    text = unicodedata.normalize("NFKC", text).casefold()
    return " ".join(re.findall(r"[^\W_]+", text, flags=re.UNICODE))


def levenshtein(ref, got):
    prev = list(range(len(got) + 1))
    for i, expected in enumerate(ref, 1):
        current = [i]
        for j, actual in enumerate(got, 1):
            current.append(min(
                prev[j] + 1,
                current[j - 1] + 1,
                prev[j - 1] + (expected != actual),
            ))
        prev = current
    return prev[-1]


def metrics(reference, hypothesis):
    ref, hyp = normalized(reference), normalized(hypothesis)
    if not ref or not hyp:
        raise ValueError("Empty reference or recognition hypothesis")
    words = ref.split()
    return {
        "schema": "libaudition.asr-quality.v1",
        "reference": ref,
        "recognized": hyp,
        "word_errors": levenshtein(words, hyp.split()),
        "reference_words": len(words),
        "wer": round(levenshtein(words, hyp.split()) / len(words), 6),
        "character_errors": levenshtein(ref.replace(" ", ""),
                                        hyp.replace(" ", "")),
        "reference_characters": len(ref.replace(" ", "")),
        "cer": round(levenshtein(ref.replace(" ", ""),
                                 hyp.replace(" ", "")) /
                     len(ref.replace(" ", "")), 6),
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--report", type=Path)
    p.add_argument("--reference")
    p.add_argument("--output", type=Path)
    p.add_argument("--max-wer", type=float)
    p.add_argument("--max-cer", type=float)
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        assert levenshtein(["a", "b"], ["a", "c"]) == 1
        assert metrics("Hello, World!", "hello world")["wer"] == 0
        assert metrics("abc", "abd")["cer"] == 1 / 3 or abs(
            metrics("abc", "abd")["cer"] - 1 / 3) < 1e-5
        print("speech-scoring-self-test=ok")
        return
    if args.report is None or args.reference is None or args.output is None:
        p.error("--report --reference --output are mandatory")
    raw = args.report.read_text(encoding="utf-8")
    values = dict(line.split("=", 1) for line in raw.splitlines() if "=" in line)
    if "asr_text" not in values:
        raise ValueError("ASR report contains no transcript")
    if int(values["vad_frames"]) < 1 or int(values["vad_active_frames"]) < 1:
        raise ValueError("Real speech fixture did not trigger Silero VAD")
    report = metrics(args.reference, values["asr_text"])
    report["vad_active_frames"] = int(values["vad_active_frames"])
    report["vad_frames"] = int(values["vad_frames"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    print(json.dumps(report, sort_keys=True))
    for threshold, name in [(args.max_wer, "wer"), (args.max_cer, "cer")]:
        if threshold is not None:
            if not 0 <= threshold <= 1 or report[name] > threshold:
                raise AssertionError(f"{name} above calibrated limit: {report[name]}")


if __name__ == "__main__":
    main()
