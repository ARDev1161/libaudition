#!/usr/bin/env python3
"""S2 real utterance placement, preserving complete starts and dry references.

The manifest must come from the licensed corpus validator. Every voice
interval is bound to a different whole utterance of the same actual speaker,
from ONE declared development/evaluation split. This does not infer true
phoneme/word onset times from the WAV, nor estimate source separation.
"""
from __future__ import annotations

from array import array
import hashlib
import json
from pathlib import Path
import sys
import wave

from acoustix_speech_corpus import validate_corpus, validated_relative_path

SCHEMA = "libaudition.acoustix-speech-plan.v1"


def _read_pcm16(path: Path) -> array:
    with wave.open(str(path), "rb") as reader:
        if (reader.getnchannels(), reader.getsampwidth(),
                reader.getframerate(), reader.getcomptype()) != (1, 2, 16000, "NONE"):
            raise ValueError("Speech input WAV must be mono 16 kHz PCM16")
        frames = reader.getnframes()
        raw = reader.readframes(frames)
    if len(raw) != frames * 2:
        raise ValueError("Speech input WAV data truncated")
    samples = array("h")
    samples.frombytes(raw)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def place_speech_intervals(scene: dict, corpus: dict, corpus_root: Path,
                           plan: dict, *, corpus_sha256: str):
    """Return {source_id: complete dry int16 PCM}, manifest provenance.

    Integer-aligned onset and clean reference samples are exact. A clip
    longer than its speaking interval FAILS rather than cutting its first
    or last word or covertly changing the source activity schedule.
    """
    validate_corpus(corpus, corpus_root, check_wav=True)
    if not isinstance(plan, dict) or plan.get("schema") != SCHEMA:
        raise ValueError("Unsupported S2 speech plan schema")
    if plan.get("corpus_manifest_sha256") != corpus_sha256:
        raise ValueError("Speech plan is pinned to another corpus manifest")
    split = plan.get("split")
    if split not in {"development", "evaluation"}:
        raise ValueError("S2 scene plan requires a single declared corpus split")
    if scene["sample_rate_hz"] != 16000:
        raise ValueError("S2 source PCM clock must be 16 kHz")
    frames = round(float(scene["duration_s"]) * 16000)
    voices = {s["id"]: s for s in scene["sources"] if s["kind"] == "voice"}
    expected = {(sid, i) for sid, src in voices.items()
                for i in range(len(src["active_s"]))}
    speakers = {s["source_id"]: s for s in corpus["speakers"]}
    clips = {}
    for source_id, speaker in speakers.items():
        for utterance in speaker["utterances"]:
            clips[(source_id, utterance["utterance_id"])] = (speaker, utterance)
    events = plan.get("segments")
    if not isinstance(events, list) or len(events) != len(expected):
        raise ValueError("Every declared voice interval needs one S2 utterance")
    seen_intervals, seen_utterances = set(), set()
    buffers = {sid: array("h", [0]) * frames for sid in voices}
    evidence = []
    for event in events:
        if not isinstance(event, dict):
            raise ValueError("Invalid speech event metadata")
        sid = event.get("source_id")
        index = event.get("interval_index")
        uid = event.get("utterance_id")
        key = (sid, index)
        if (type(index) is not int or key not in expected
                or key in seen_intervals or (sid, uid) not in clips):
            raise ValueError("Unknown, repeated or mismatched voice interval/utterance")
        if uid in seen_utterances:
            raise ValueError("Speech utterance reused within same scene")
        seen_intervals.add(key)
        seen_utterances.add(uid)
        speaker, utterance = clips[(sid, uid)]
        if utterance["split"] != split:
            raise ValueError("Speech utterance belongs to another corpus split")
        if speaker["source_id"] != sid:
            raise ValueError("Speech speaker/source identity mismatch")
        start_s, stop_s = voices[sid]["active_s"][index]
        begin, end = round(start_s * 16000), round(stop_s * 16000)
        if begin < 0 or end > frames or begin >= end:
            raise ValueError("Invalid S2 speaking interval sample boundaries")
        samples = _read_pcm16(validated_relative_path(corpus_root, utterance["path"]))
        if not samples or len(samples) > end - begin:
            raise ValueError("Complete speech utterance does not fit interval; no truncation permitted")
        buffers[sid][begin:begin + len(samples)] = samples
        evidence.append({
            "source_id": sid,
            "speaker_id": speaker["speaker_id"],
            "utterance_id": uid, "split": split,
            "transcript": utterance["transcript"],
            "speech_interval_index": index,
            "scheduled_start_frame": begin,
            "scheduled_end_frame": end,
            "original_utterance_frames": len(samples),
            "placed_end_frame": begin + len(samples),
            "clean_source_sha256": utterance["sha256"],
        })
    if seen_intervals != expected:
        raise ValueError("S2 speech plan omits a voice interval")
    if not any(any(x for x in buf) for buf in buffers.values()):
        raise ValueError("No non-silent human corpus audio selected")
    return buffers, {
        "schema": "libaudition.acoustix-speech-placement.v1",
        "corpus_manifest_sha256": corpus_sha256,
        "split": split,
        "sample_rate_hz": 16000,
        "voice_source_count": len(voices),
        "speech_event_count": len(evidence),
        "segments": sorted(evidence, key=lambda e: (
            e["source_id"], e["speech_interval_index"])),
        "limits": [
            "intact source clips scheduled at exact declared interval start",
            "no detection of actual phonetic onset within source clip",
            "no implicit fade or word truncation",
            "no measured voice separation, leakage or ASR WER",
        ],
    }


def load_scene_speech(scene: dict, manifest_path: Path, corpus_root: Path,
                      plan_path: Path):
    original = manifest_path.read_bytes()
    corpus = json.loads(original)
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    buffers, evidence = place_speech_intervals(
        scene, corpus, corpus_root, plan,
        corpus_sha256=hashlib.sha256(original).hexdigest())
    evidence["scene_speech_plan_sha256"] = hashlib.sha256(
        plan_path.read_bytes()).hexdigest()
    return buffers, evidence


def write_clean_references(root: Path, buffers: dict[str, array],
                           *, sample_rate: int = 16000) -> dict[str, str]:
    if sample_rate != 16000:
        raise ValueError("Invalid S2 dry reference clock")
    root.mkdir(parents=True, exist_ok=True)
    sha = {}
    for source_id, pcm in sorted(buffers.items()):
        raw = array("h", pcm)
        if sys.byteorder != "little":
            raw.byteswap()
        target = root / (source_id + ".wav")
        with wave.open(str(target), "wb") as writer:
            writer.setnchannels(1)
            writer.setsampwidth(2)
            writer.setframerate(sample_rate)
            writer.writeframes(raw.tobytes())
        sha[source_id] = hashlib.sha256(target.read_bytes()).hexdigest()
    return sha
