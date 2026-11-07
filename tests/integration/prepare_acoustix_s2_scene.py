#!/usr/bin/env python3
"""Build explicit reproducible 1/2/7 real-speech Acoustix scenes and plans.

No audio is downloaded or fabricated. Each whole utterance is taken from
the frozen licensed corpus, and its exact duration sets a new, separate S2
speaking interval. The frozen S1 source geometry and trajectory remain the
model reference; this never changes the S1 held-out scene manifest.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path

from acoustix_scene import check_scene
from acoustix_speech_corpus import validate_corpus
from acoustix_speech_placement import SCHEMA as PLAN_SCHEMA
from acoustix_speech_placement import place_speech_intervals

SOURCE_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCENE = SOURCE_ROOT / "fixtures" / "acoustix" / "table_conversation_7x7.json"
SPEAKER_COUNTS = (1, 2, 7)
SPLITS = ("development", "evaluation")
MOTION = ("stationary", "moving")


def build_scene_and_plan(base_scene: dict, corpus: dict, corpus_root: Path,
                         *, corpus_sha: str, speakers: int, split: str,
                         robot_motion: str, include_background: bool = True,
                         rt60_s: float = 0.36):
    if speakers not in SPEAKER_COUNTS:
        raise ValueError("S2 first-stage scene supports 1, 2, or 7 voices")
    if split not in SPLITS or robot_motion not in MOTION:
        raise ValueError("Unsupported S2 split or robot motion")
    if rt60_s not in (0.2, 0.36, 0.7):
        raise ValueError("S2 target RT60 must be 0.2, 0.36, or 0.7 s")
    validate_corpus(corpus, corpus_root)
    scene = copy.deepcopy(base_scene)
    if scene.get("schema") != "libaudition.acoustix-scene.v1":
        raise ValueError("Wrong base Acoustix scene")
    fs = scene["sample_rate_hz"]
    duration_frames = round(scene["duration_s"] * fs)
    selected = {f"speaker_{i}" for i in range(1, speakers + 1)}
    corpus_by_id = {s["source_id"]: s for s in corpus["speakers"]}
    intervals = []
    scene["description"] = (
        f"S2 complete licensed human speech, voices={speakers}, "
        f"split={split}, robot_motion={robot_motion}, background={include_background}")
    scene["room"]["rt60_s"] = rt60_s
    if robot_motion == "stationary":
        initial = copy.deepcopy(scene["robot"]["trajectory"][0])
        last = copy.deepcopy(initial)
        last["t_s"] = scene["duration_s"]
        scene["robot"]["trajectory"] = [initial, last]
    sources = []
    for source in scene["sources"]:
        if source["kind"] != "voice":
            if include_background:
                sources.append(source)
            continue
        if source["id"] not in selected:
            continue
        speaker = corpus_by_id[source["id"]]
        matches = [clip for clip in speaker["utterances"] if clip["split"] == split]
        if not matches:
            raise ValueError("Missing split-specific real utterance for " + source["id"])
        # Initial occurrence is the natural schedule anchor; not chosen
        # using ODAS predictions, VAD results or the word transcript.
        if not source["active_s"]:
            raise ValueError("No scene speech timing anchor")
        begin = round(float(source["active_s"][0][0]) * fs)
        # Explicit silence padding after each complete utterance; never crop.
        tail_padding_frames = round(0.1 * fs)
        candidates = []
        for clip in matches:
            from acoustix_speech_corpus import pcm16_summary, validated_relative_path
            wav = validated_relative_path(corpus_root, clip["path"])
            frames = pcm16_summary(wav)["frames"]
            if begin + frames + tail_padding_frames <= duration_frames:
                candidates.append((clip, frames))
        if not candidates:
            raise ValueError(
                "No complete utterance fits the scene duration: " + source["id"])
        clip, clip_frames = sorted(candidates, key=lambda x: x[0]["utterance_id"])[0]
        end = begin + clip_frames + tail_padding_frames
        source["active_s"] = [[begin / fs, end / fs]]
        sources.append(source)
        intervals.append({
            "source_id": source["id"],
            "interval_index": 0,
            "utterance_id": clip["utterance_id"],
        })
    scene["sources"] = sources
    check_scene(scene)
    if len(intervals) != speakers:
        raise ValueError("S2 scene has fewer selected speakers than declared")
    plan = {
        "schema": PLAN_SCHEMA,
        "corpus_manifest_sha256": corpus_sha,
        "split": split,
        "segments": sorted(intervals, key=lambda s: s["source_id"]),
    }
    _, schedule = place_speech_intervals(
        scene, corpus, corpus_root, plan, corpus_sha256=corpus_sha)
    return scene, plan, {
        "schema": "libaudition.acoustix-s2-plan-build.v1",
        "status": "CORPUS_PCM_VALIDATED_NOT_RENDERED",
        "speaker_count": speakers,
        "split": split,
        "robot_motion": robot_motion,
        "background_sources": [s["id"] for s in sources if s["kind"] != "voice"],
        "requested_rt60_s": rt60_s,
        "corpus_manifest_sha256": corpus_sha,
        "scene_audio_provenance": schedule,
        "limitations": [
            "Not yet rendered through Acoustix CPU RIR",
            "No known actual phonetic word-onset timestamps",
            "No SI-SDRi, SIRi or WER computed",
            "Frozen S1 held-out configurations not modified",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-manifest", type=Path, required=True)
    parser.add_argument("--corpus-root", type=Path, required=True)
    parser.add_argument("--base-scene", type=Path, default=DEFAULT_SCENE)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--speakers", type=int, required=True, choices=SPEAKER_COUNTS)
    parser.add_argument("--split", choices=SPLITS, required=True)
    parser.add_argument("--robot-motion", choices=MOTION, default="moving")
    parser.add_argument("--no-background", action="store_true")
    parser.add_argument("--rt60-s", type=float, choices=(0.2, 0.36, 0.7), default=0.36)
    args = parser.parse_args()
    raw = args.corpus_manifest.read_bytes()
    scene, plan, report = build_scene_and_plan(
        json.loads(args.base_scene.read_text(encoding="utf-8")),
        json.loads(raw), args.corpus_root,
        corpus_sha=hashlib.sha256(raw).hexdigest(),
        speakers=args.speakers, split=args.split,
        robot_motion=args.robot_motion,
        include_background=not args.no_background,
        rt60_s=args.rt60_s)
    args.output.mkdir(parents=True, exist_ok=True)
    files = {
        "scene.json": json.dumps(scene, indent=2, sort_keys=True) + "\n",
        "speech_plan.json": json.dumps(plan, indent=2, sort_keys=True) + "\n",
    }
    for name in files:
        if (args.output / name).exists():
            raise FileExistsError("Frozen S2 scenario already exists: " + name)
    report["scene_sha256"] = hashlib.sha256(files["scene.json"].encode()).hexdigest()
    report["speech_plan_sha256"] = hashlib.sha256(
        files["speech_plan.json"].encode()).hexdigest()
    files["scene_plan_audit.json"] = json.dumps(report, indent=2, sort_keys=True) + "\n"
    for name in files:
        if (args.output / name).exists():
            raise FileExistsError("Frozen S2 scenario already exists: " + name)
    for name in files:
        (args.output / name).write_text(files[name], encoding="utf-8")
    print(json.dumps({"output": str(args.output), "speakers": args.speakers,
                      "split": args.split, "status": report["status"]}))


if __name__ == "__main__":
    main()
