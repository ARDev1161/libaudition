#!/usr/bin/env python3
"""Frozen S1 held-out CPU acoustics evaluation manifest (configuration only).

Each item is a reproducible 1.5-second simulated room recipe. This is NOT a
completed 20-seed CPU render, S2 human speech validation, or calibrated room.
Changing these definitions after evaluating algorithms would invalidate the
held-out split; review changes separately rather than silently regenerating.
"""
from __future__ import annotations

import hashlib
import json

from acoustix_scene import check_scene
from test_acoustix_replay import encoded_scene, make_scene

SCHEMA = "libaudition.acoustix-s1-heldout-manifest.v1"
SEEDS = tuple(range(5161, 5181))
ROOM_RECIPES = ("static_one_voice", "moving_two_voices",
                "larger_room_three_voices")
RT60_TARGETS = (0.20, 0.36, 0.70)


def entries() -> list[dict]:
    items = []
    for i, seed in enumerate(SEEDS):
        recipe = ROOM_RECIPES[i % len(ROOM_RECIPES)]
        nominal_rt60 = RT60_TARGETS[(i // len(ROOM_RECIPES)) % len(RT60_TARGETS)]
        scene = make_scene(recipe)
        scene["room"]["rt60_s"] = nominal_rt60
        scene["description"] = ("S1 frozen held-out physics evaluation; "
                                "not S2 actual speech: " + recipe)
        contract = check_scene(scene)
        items.append({
            "case_id": f"s1_eval_{i + 1:02d}",
            "seed": seed,
            "recipe": recipe,
            "requested_rt60_s": nominal_rt60,
            "scene_sha256": hashlib.sha256(encoded_scene(scene)).hexdigest(),
            "speech_source_count": contract["voice_sources"],
            "sample_rate_hz": contract["sample_rate_hz"],
            "duration_s": contract["duration_s"],
            "moving_microphone_array": recipe != "static_one_voice",
        })
    return items


def verify_manifest(items: list[dict]) -> None:
    if len(items) != 20:
        raise ValueError("Held-out S1 manifest must contain exactly 20 scenarios")
    if [item["seed"] for item in items] != list(SEEDS):
        raise ValueError("Held-out seeds or ordering have changed")
    if len({item["case_id"] for item in items}) != 20:
        raise ValueError("Duplicate held-out scenario identity")
    if len({(item["scene_sha256"], item["seed"]) for item in items}) != 20:
        raise ValueError("Duplicated scene/seed combination")
    if set(item["recipe"] for item in items) != set(ROOM_RECIPES):
        raise ValueError("Missing one of the frozen room recipes")
    if set(item["requested_rt60_s"] for item in items) != set(RT60_TARGETS):
        raise ValueError("Missing low/mid/high RT60 acoustic slice")
    if not {1, 2, 3}.issubset({item["speech_source_count"] for item in items}):
        raise ValueError("Missing one/two/three-source acoustic scenes")
    if not {True, False}.issubset({item["moving_microphone_array"] for item in items}):
        raise ValueError("Missing moving or stationary robot")
    if any(item["sample_rate_hz"] != 16000 or item["duration_s"] != 1.5
           for item in items):
        raise ValueError("Unexpected PCM timebase")


def render_manifest() -> str:
    items = entries()
    verify_manifest(items)
    return json.dumps({
        "schema": SCHEMA,
        "status": "CONFIGURATION_ONLY_NOT_RENDERED",
        "evaluation_split": "held_out_s1_physics",
        "cases": items,
        "limitations": [
            "Not evidence of 20 successful CPU RIR renders",
            "Synthetic voiced excitations, not intelligible speech",
            "No target/interferer SNR fixture yet; never claim SNR coverage",
            "S2 requires independent licensed voices and transcripts",
            "No physical microphone/room calibration"
        ],
    }, sort_keys=True, indent=2) + "\n"
