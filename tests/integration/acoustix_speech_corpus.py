#!/usr/bin/env python3
"""Strict S2 real-speech corpus ingress contract (pure standard library).

This validates evidence about user-supplied 16 kHz PCM16 speech, NOT that a
recording is a human voice or that an ASR transcript is correct. CI regression
fixtures use artificially generated WAV only to test rejection behavior; they
MUST NOT be used as a speech-quality benchmark.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
import wave

SCHEMA = "libaudition.acoustix-speech-corpus.v1"
SPLITS = {"development", "evaluation"}
REQUIRED_SOURCE_IDS = {f"speaker_{index}" for index in range(1, 8)}
HEX_SHA256 = re.compile(r"[0-9a-f]{64}\Z")
SAFE_AUDIO_FILE = re.compile(r"[A-Za-z0-9_-]+\.wav\Z")
SAFE_UTTERANCE_ID = re.compile(r"[A-Za-z0-9_-]+\Z")
SAFE_SPEAKER_ID = re.compile(r"[A-Za-z0-9_-]+\Z")
MAX_BYTES = 16000 * 2 * 40  # Bound untrusted mono WAV to 40 seconds.


def validated_relative_path(root: Path, name: str) -> Path:
    if not isinstance(name, str) or not name or "\\" in name:
        raise ValueError("Unsafe speech clip path")
    relative = PurePosixPath(name)
    if (relative.is_absolute() or len(relative.parts) < 2
            or any(p in ("", ".", "..") for p in relative.parts)
            or not SAFE_AUDIO_FILE.fullmatch(relative.name)):
        raise ValueError("Unsafe speech clip path")
    absolute_root = root.resolve()
    resolved = (absolute_root / relative).resolve()
    if not resolved.is_relative_to(absolute_root):
        raise ValueError("Speech clip path escapes corpus root")
    return resolved


def pcm16_summary(path: Path) -> dict:
    if not path.is_file() or path.stat().st_size > MAX_BYTES + 4096:
        raise ValueError("Missing/oversized WAV source")
    with wave.open(str(path), "rb") as reader:
        if (reader.getnchannels() != 1 or reader.getsampwidth() != 2
                or reader.getframerate() != 16000
                or reader.getcomptype() != "NONE"):
            raise ValueError("Speech WAV must be mono PCM16 16 kHz")
        frames = reader.getnframes()
        if not 16000 // 2 <= frames <= 16000 * 30:
            raise ValueError("Speech utterance duration must be 0.5-30 seconds")
        pcm = reader.readframes(frames)
    if len(pcm) != frames * 2:
        raise ValueError("Truncated speech WAV data")
    total_sq = 0
    clipped = 0
    nonzero = 0
    for (sample,) in struct.iter_unpack("<h", pcm):
        total_sq += sample * sample
        clipped += abs(sample) >= 32767
        nonzero += sample != 0
    rms = math.sqrt(total_sq / frames) / 32768.0
    if rms <= 1e-4 or nonzero < 160:
        raise ValueError("Speech clip is silent or implausibly sparse")
    if clipped:
        raise ValueError("Clipped speech PCM is not a clean reference")
    return {"frames": frames, "duration_s": frames / 16000.0,
            "rms_dbfs": 20 * math.log10(rms), "nonzero_frames": nonzero}


def validate_corpus(manifest: dict, root: Path, *, check_wav: bool = True) -> dict:
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA:
        raise ValueError("Unsupported S2 speech corpus schema")
    provenance = manifest.get("provenance")
    if not isinstance(provenance, dict):
        raise ValueError("Corpus license and provenance are required")
    required = {"corpus_name", "source_url", "license_id", "language", "license_url"}
    if any(not isinstance(provenance.get(key), str)
           or not provenance[key].strip() for key in required):
        raise ValueError("Incomplete speech license/provenance attribution")
    if provenance["license_id"] not in {"CC-BY-4.0", "CC0-1.0"}:
        raise ValueError("S2 corpus license must allow audio redistribution")
    if not provenance["source_url"].startswith("https://") or not provenance["license_url"].startswith("https://"):
        raise ValueError("Provenance URLs must use HTTPS")
    sources = manifest.get("speakers")
    if not isinstance(sources, list) or len(sources) != 7:
        raise ValueError("S2 corpus needs exactly seven distinct speakers")
    seen_sources, seen_speakers = set(), set()
    seen_utterances, seen_paths, seen_hashes = set(), set(), set()
    records = []
    for speaker in sources:
        if not isinstance(speaker, dict):
            raise ValueError("Malformed speaker metadata")
        sid, identity = speaker.get("source_id"), speaker.get("speaker_id")
        if (sid not in REQUIRED_SOURCE_IDS or not isinstance(identity, str)
                or not SAFE_SPEAKER_ID.fullmatch(identity)):
            raise ValueError("Invalid S2 source/speaker identity")
        if sid in seen_sources or identity in seen_speakers:
            raise ValueError("Duplicate source or actual speaker identity")
        seen_sources.add(sid)
        seen_speakers.add(identity)
        clips = speaker.get("utterances")
        if not isinstance(clips, list) or len(clips) < 3:
            raise ValueError("Each speaker needs at least three distinct utterances")
        splits = {"development": 0, "evaluation": 0}
        for clip in clips:
            if not isinstance(clip, dict):
                raise ValueError("Malformed speech utterance")
            uid, subset = clip.get("utterance_id"), clip.get("split")
            if (not isinstance(uid, str)
                    or not SAFE_UTTERANCE_ID.fullmatch(uid) or subset not in SPLITS):
                raise ValueError("Invalid utterance ID or corpus split")
            transcript = clip.get("transcript")
            if not isinstance(transcript, str) or len(transcript.strip()) < 3:
                raise ValueError("Every speech utterance needs a real transcript")
            if any(ord(ch) < 32 for ch in transcript):
                raise ValueError("Transcript contains control characters")
            sha = clip.get("sha256")
            if not isinstance(sha, str) or not HEX_SHA256.fullmatch(sha):
                raise ValueError("Every utterance needs a fixed SHA-256 digest")
            path_str = clip.get("path")
            wav_path = validated_relative_path(root, path_str)
            if uid in seen_utterances or path_str in seen_paths or sha in seen_hashes:
                raise ValueError("Reused speech utterance, path or audio SHA-256")
            seen_utterances.add(uid)
            seen_paths.add(path_str)
            seen_hashes.add(sha)
            splits[subset] += 1
            summary = None
            if check_wav:
                actual = hashlib.sha256(wav_path.read_bytes()).hexdigest()
                if actual != sha:
                    raise ValueError("Speech file SHA-256 mismatch: " + uid)
                summary = pcm16_summary(wav_path)
            records.append({"source_id": sid, "speaker_id": identity,
                            "utterance_id": uid, "split": subset,
                            "sha256": sha, "pcm": summary})
        if splits["development"] < 1 or splits["evaluation"] < 2:
            raise ValueError("Each speaker needs >=1 development and >=2 evaluation utterances")
    if seen_sources != REQUIRED_SOURCE_IDS:
        raise ValueError("S2 speaker IDs must map to speaker_1..speaker_7")
    return {
        "schema": "libaudition.acoustix-speech-corpus-audit.v1",
        "metadata_valid": True, "pcm_checked": check_wav,
        "license_id": provenance["license_id"],
        "distinct_speaker_count": len(seen_speakers),
        "distinct_utterance_count": len(records),
        "development_count": sum(x["split"] == "development" for x in records),
        "evaluation_count": sum(x["split"] == "evaluation" for x in records),
        "utterances": records,
        "not_validated": [
            "human speech authenticity and read-aloud transcript correctness",
            "individual speaker identity beyond supplied corpus labels",
            "source placement and lip/word onset timing",
            "source separation SI-SDRi/SIRi",
            "ASR WER and quiet speech recognition",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True,
                        help="Directory containing WAV clips, never the raw FLAC archive")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    passed = False
    report = None
    error = None
    try:
        report = validate_corpus(
            json.loads(args.manifest.read_text(encoding="utf-8")), args.root)
        passed = True
    except (ValueError, OSError, wave.Error, KeyError, TypeError) as exc:
        error = f"{type(exc).__name__}: {exc}"
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({
        "passed": passed, "error": error, "evidence": report,
    }, indent=2) + "\n")
    if not passed:
        raise SystemExit("S2 corpus audit FAILED: " + str(error))


if __name__ == "__main__":
    main()
