#!/usr/bin/env python3
"""Deterministically prepare seven actual speakers from local LibriSpeech FLACs.

Input is a *user-acquired* official LibriSpeech dev-clean tree. This utility
does not download datasets, invent transcripts or upload anybody's audio.
It converts complete utterances, never cuts off a word, writes source WAVs
and an independently auditable S2 corpus manifest. An "evaluation" split
here means held-out UTTERANCES within dev-clean, not official test-clean or
held-out speakers.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from acoustix_speech_corpus import SCHEMA, pcm16_summary, validate_corpus


def transcripts_for_speaker(folder: Path):
    rows = {}
    for transcript_file in sorted(folder.rglob("*.trans.txt")):
        for entry in transcript_file.read_text(encoding="utf-8").splitlines():
            utterance_id, sep, words = entry.partition(" ")
            if (not sep or not words.strip() or
                    not utterance_id.startswith(folder.name + "-")):
                raise ValueError("Malformed LibriSpeech transcript entry")
            if utterance_id in rows:
                raise ValueError("Duplicate LibriSpeech utterance ID")
            path = transcript_file.parent / (utterance_id + ".flac")
            if not path.is_file() or not path.resolve().is_relative_to(folder.resolve()):
                raise ValueError("Missing or unsafe LibriSpeech FLAC: " + utterance_id)
            rows[utterance_id] = (path, words.strip())
    return sorted(rows.items())


def convert_clip(ffmpeg: str, source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin",
         "-y", "-i", str(source), "-map", "0:a:0",
         "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le", str(target)],
        check=True, capture_output=True, timeout=60)


def prepare(root: Path, output: Path, ffmpeg: str, *,
            speaker_count: int = 7, min_duration_s: float = 0.6,
            max_duration_s: float = 5.0) -> dict:
    root = root.resolve()
    output = output.resolve()
    if (speaker_count != 7 or min_duration_s < 0.5
            or min_duration_s >= max_duration_s or max_duration_s > 30):
        raise ValueError("Require 7 speakers and valid complete-utterance limits")
    if not root.is_dir() or not shutil.which(ffmpeg):
        raise ValueError("LibriSpeech dev-clean tree and ffmpeg are required")
    speakers = []
    for speaker_dir in sorted(root.iterdir(), key=lambda x: x.name):
        if not speaker_dir.is_dir() or not speaker_dir.name.isdigit():
            continue
        candidates = transcripts_for_speaker(speaker_dir)
        if not candidates:
            continue
        source_id = f"speaker_{len(speakers) + 1}"
        utterances = []
        for utterance_id, (original, words) in candidates:
            target = output / source_id / (utterance_id + ".wav")
            if target.exists():
                raise FileExistsError(
                    "Frozen output exists; move it before regenerating: " + str(target))
            convert_clip(ffmpeg, original, target)
            try:
                metrics = pcm16_summary(target)
                if not min_duration_s <= metrics["duration_s"] <= max_duration_s:
                    target.unlink()
                    continue
            except ValueError:
                target.unlink()
                continue
            utterances.append({
                "utterance_id": utterance_id,
                "path": f"{source_id}/{utterance_id}.wav",
                "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                "split": "development" if not utterances else "evaluation",
                "transcript": words,
            })
            if len(utterances) == 3:
                break
        if len(utterances) < 3:
            for clip in utterances:
                (output / clip["path"]).unlink()
            continue
        speakers.append({
            "source_id": source_id, "speaker_id": speaker_dir.name,
            "utterances": utterances,
        })
        if len(speakers) == 7:
            break
    if len(speakers) != 7:
        raise ValueError("Could not find 7 distinct speakers with 3 complete valid utterances")
    manifest = {
        "schema": SCHEMA,
        "provenance": {
            "corpus_name": "LibriSpeech dev-clean (internal utterance-heldout selection)",
            "source_url": "https://www.openslr.org/12/",
            "license_id": "CC-BY-4.0",
            "license_url": "https://creativecommons.org/licenses/by/4.0/",
            "language": "en",
        },
        "speakers": speakers,
    }
    validate_corpus(manifest, output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--librispeech-dir", type=Path, required=True,
                        help="Extracted LibriSpeech/dev-clean root")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--min-duration-s", type=float, default=0.6)
    parser.add_argument("--max-duration-s", type=float, default=5.0)
    args = parser.parse_args()
    manifest = prepare(args.librispeech_dir, args.output, args.ffmpeg,
                       min_duration_s=args.min_duration_s,
                       max_duration_s=args.max_duration_s)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n",
                             encoding="utf-8")
    print(json.dumps({
        "speakers": len(manifest["speakers"]),
        "utterances": sum(len(s["utterances"]) for s in manifest["speakers"]),
        "corpus_manifest": str(args.manifest),
        "input_source": "LibriSpeech dev-clean, CC BY 4.0",
    }))


if __name__ == "__main__":
    main()
