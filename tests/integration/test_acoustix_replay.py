#!/usr/bin/env python3
"""S1 Acoustix replay determinism and independent geometry regression.

Lightweight default mode validates three frozen scene recipes and the manifest
contract using stdlib only. --cpu-rir repeats real pinned CPU RIR renders,
compares exact WAV/stem/oracle hashes and tests meaningful seed sensitivity.
No ODAS tracking results are used in these simulator acceptance checks.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from acoustix_scene import check_scene
from acoustix_timing_contract import verify_timing

ROOT = Path(__file__).resolve().parents[1]
BASE_SCENE = ROOT / "fixtures" / "acoustix" / "table_conversation_7x7.json"
SEEDS = (1161, 2161, 3161)
REPORT: dict = {}


def make_scene(case_name: str) -> dict:
    """Create independent, fully declared short rooms from the pinned fixture.

    No speaker audio, geometry, oracle or ODAS output is sampled to adapt the
    acoustic model to the algorithm under evaluation.
    """
    original = json.loads(BASE_SCENE.read_text(encoding="utf-8"))
    scene = copy.deepcopy(original)
    duration = 1.5
    scene["duration_s"] = duration
    scene["description"] = "S1 deterministic replay " + case_name

    if case_name == "static_one_voice":
        keep = {"speaker_1", "fridge"}
        scene["room"]["rt60_s"] = 0.20
        initial = scene["robot"]["trajectory"][0]
        scene["robot"]["trajectory"] = [
            copy.deepcopy(initial),
            {"t_s": duration, "position_m": list(initial["position_m"]),
             "yaw_deg": initial["yaw_deg"]},
        ]
    elif case_name == "moving_two_voices":
        keep = {"speaker_1", "speaker_2", "fridge", "ac"}
        scene["room"]["rt60_s"] = 0.36
        scene["robot"]["trajectory"] = scene["robot"]["trajectory"][:2]
    elif case_name == "larger_room_three_voices":
        keep = {"speaker_1", "speaker_2", "speaker_3", "fridge"}
        scene["room"]["dimensions_m"] = [8.0, 7.0, 3.0]
        scene["room"]["rt60_s"] = 0.20
        scene["robot"]["trajectory"] = scene["robot"]["trajectory"][:2]
        # Rotate the microphone offsets in the robot body frame, without
        # changing their radius, to exercise a different array orientation.
        scene["robot"]["microphones_m"] = [
            [-offset[1], offset[0], offset[2]]
            for offset in scene["robot"]["microphones_m"]
        ]
    else:
        raise ValueError("Unsupported frozen S1 scenario: " + case_name)

    selected = []
    for source in scene["sources"]:
        if source["id"] not in keep:
            continue
        source = copy.deepcopy(source)
        source["active_s"] = [
            [max(0., float(begin)), min(duration, float(end))]
            for begin, end in source["active_s"]
            if float(begin) < duration and float(end) > 0
        ]
        if not source["active_s"]:
            raise ValueError("Frozen S1 source has no active interval")
        selected.append(source)
    scene["sources"] = selected
    check_scene(scene)
    return scene


def encoded_scene(scene: dict) -> bytes:
    return (json.dumps(scene, sort_keys=True,
                       indent=2, ensure_ascii=True) + "\n").encode("utf-8")


def file_sha(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as reader:
        for block in iter(lambda: reader.read(1 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()


def artifact_signatures(folder: Path, source_ids: list[str]) -> dict:
    names = ["input.wav", "ground_truth.json", "rt60_calibration.json",
             "signal_audit.json"]
    names += ["stems/" + name + ".wav" for name in source_ids]
    signatures = {}
    for name in names:
        path = folder / name
        if not path.is_file() or not path.stat().st_size:
            raise AssertionError("Missing or empty replay artifact: " + str(path))
        signatures[name] = file_sha(path)
    return signatures


class SceneRecipeTests(unittest.TestCase):
    def test_distinct_valid_fixture_contracts_and_frozen_serialization(self):
        cases = ("static_one_voice", "moving_two_voices",
                 "larger_room_three_voices")
        hashes = []
        counts = []
        for case in cases:
            scene = make_scene(case)
            self.assertEqual(encoded_scene(scene), encoded_scene(make_scene(case)))
            result = check_scene(scene)
            self.assertEqual(result["microphones"], 4)
            self.assertEqual(result["duration_s"], 1.5)
            self.assertEqual(result["sample_rate_hz"], 16000)
            hashes.append(hashlib.sha256(encoded_scene(scene)).hexdigest())
            counts.append(result["voice_sources"])
        self.assertEqual(len(set(hashes)), len(cases))
        self.assertEqual(counts, [1, 2, 3])

    def test_unknown_scenario_cannot_silently_map_to_baseline(self):
        with self.assertRaises(ValueError):
            make_scene("missing")


class CpuReplayTests(unittest.TestCase):
    def test_byte_identical_two_runs_each_scene_and_seed_sensitivity(self):
        from acoustix_scene import render
        import wave

        records = {}
        with tempfile.TemporaryDirectory(prefix="acoustix-s1-replay-") as scratch:
            temp = Path(scratch)
            for index, case_name in enumerate((
                    "static_one_voice", "moving_two_voices",
                    "larger_room_three_voices")):
                case = make_scene(case_name)
                case_file = temp / (case_name + ".json")
                case_file.write_bytes(encoded_scene(case))
                source_ids = [source["id"] for source in case["sources"]]
                seed = SEEDS[index]
                case_hash = file_sha(case_file)

                observed = []
                for run_no in (1, 2):
                    folder = temp / case_name / ("run_" + str(run_no))
                    render(case, SimpleNamespace(
                        scene=case_file, output=folder, seed=seed,
                        hop_ms=250, max_seconds=None, speech_dir=None,
                        stems=True, runner=None, compare_capacity=False,
                        uncalibrated_rt60=False))
                    hashes = artifact_signatures(folder, source_ids)
                    observed.append(hashes)
                    gt = json.loads((folder / "ground_truth.json").read_text())
                    audit = json.loads((folder / "signal_audit.json").read_text())
                    time_evidence = verify_timing(case, gt, folder / "input.wav")\n                    self.assertGreater(time_evidence["oracle_hops"], 0)\n                    self.assertEqual(gt["render_seed"], seed)
                    self.assertEqual(gt["rir_hop_ms"], 250)
                    self.assertEqual(gt["scene_sha256"], case_hash)
                    self.assertEqual(gt["source_count"], len(source_ids))
                    self.assertEqual(gt["mic_count"], 4)
                    self.assertTrue(gt["ground_truth"], "Missing geometric oracle")
                    self.assertLessEqual(audit["reconstruction_max_residual_lsb"],
                                         audit["quantization_allowance_lsb"])
                    self.assertEqual(audit["source_count"], len(source_ids))
                    self.assertGreater(audit["post_acoustic_signal_silence_frames"], 0)
                    with wave.open(str(folder / "input.wav")) as reader:
                        self.assertEqual(reader.getnchannels(), 6)
                        self.assertEqual(reader.getframerate(), 16000)
                        self.assertEqual(reader.getsampwidth(), 2)
                        self.assertEqual(reader.getnframes(), gt["wav_frames"])
                self.assertEqual(
                    observed[0], observed[1],
                    f"Byte-deterministic RIR/WAV/stem/oracle failure for {case_name}")
                records[case_name] = {
                    "seed": seed,
                    "scene_sha256": case_hash,
                    "sources": source_ids,
                    "repeated_runs_equal": True,
                    "artifact_sha256": observed[0],
                }

                if index == 1:
                    # All source geometry/RT60 stays fixed. Changing a seed
                    # must change actual noise, not the deterministic voices,
                    # and must NOT change the geometric oracle.
                    changed_folder = temp / case_name / "different_seed"
                    render(case, SimpleNamespace(
                        scene=case_file, output=changed_folder, seed=seed + 1,
                        hop_ms=250, max_seconds=None, speech_dir=None,
                        stems=True, runner=None, compare_capacity=False,
                        uncalibrated_rt60=False))
                    different = artifact_signatures(changed_folder, source_ids)
                    self.assertNotEqual(observed[0]["input.wav"],
                                        different["input.wav"])
                    self.assertNotEqual(observed[0]["stems/fridge.wav"],
                                        different["stems/fridge.wav"])
                    self.assertEqual(observed[0]["stems/speaker_1.wav"],
                                     different["stems/speaker_1.wav"])
                    base_gt = json.loads((temp / case_name / "run_1" /
                                         "ground_truth.json").read_text())
                    alt_gt = json.loads(
                        (changed_folder / "ground_truth.json").read_text())
                    self.assertEqual(base_gt["ground_truth"], alt_gt["ground_truth"])
                    self.assertNotEqual(base_gt["render_seed"],
                                        alt_gt["render_seed"])
                    records[case_name]["different_seed_changes_noise"] = True
                    records[case_name]["different_seed"] = seed + 1

        REPORT["replays"] = records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu-rir", action="store_true",
                        help="Render full pinned CPU RIR/WAV/stem replay cases")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SceneRecipeTests)
    if args.cpu_rir:
        suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(
            CpuReplayTests))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({
            "schema": "libaudition.acoustix-replay-check.v1",
            "passed": True,
            "cpu_rir_exercised": args.cpu_rir,
            "scenes": [
                "static_one_voice",
                "moving_two_voices",
                "larger_room_three_voices"
            ],
            "evidence": REPORT,
            "limitations": [
                "exact same-reference-runner reproducibility only",
                "not validated across CPU/OS/BLAS versions",
                "synthetic voiced tones, not intelligible speech",
                "not a 20+ held-out speech benchmark",
                "no real-world RIR/hardware correlation",
            ],
        }, indent=2) + "\n")


if __name__ == "__main__":
    main()
