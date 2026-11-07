#!/usr/bin/env python3
"""Real pinned Acoustix RIR test for explicitly controlled background SNR."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
import wave

from acoustix_scene import render
from test_acoustix_replay import encoded_scene, make_scene


def read_mic0(path: Path):
    import numpy as np
    with wave.open(str(path), "rb") as reader:
        if reader.getnchannels() != 4 or reader.getsampwidth() != 2:
            raise ValueError("Expected 4-channel PCM16 stems")
        data = np.frombuffer(reader.readframes(reader.getnframes()), dtype="<i2")
        return data.reshape(-1, 4)[:, 0].astype(np.float64)


def measured_pcm_snr(folder: Path, scene: dict) -> float:
    import numpy as np
    voice = None
    background = None
    for source in scene["sources"]:
        samples = read_mic0(folder / "stems" / (source["id"] + ".wav"))
        if source["kind"] == "voice":
            voice = samples if voice is None else voice + samples
        else:
            background = samples if background is None else background + samples
    frames = int(round(scene["duration_s"] * scene["sample_rate_hz"]))
    mask = np.zeros(frames, dtype=bool)
    for source in scene["sources"]:
        if source["kind"] != "voice":
            continue
        for begin, end in source["active_s"]:
            mask[round(begin * scene["sample_rate_hz"]):
                 round(end * scene["sample_rate_hz"])] = True
    power_voice = np.mean(voice[:frames][mask] ** 2)
    power_background = np.mean(background[:frames][mask] ** 2)
    return 10.0 * math.log10(power_voice / power_background)


class CpuBackgroundSnr(unittest.TestCase):
    def test_real_rir_pcm_snr_targets(self):
        scene = make_scene("static_one_voice")
        with tempfile.TemporaryDirectory(prefix="acoustix-background-snr-") as temp:
            root = Path(temp)
            scene_file = root / "scene.json"
            scene_file.write_bytes(encoded_scene(scene))
            for target in (0.0, 6.0, 12.0):
                out = root / ("snr_" + str(int(target)))
                render(scene, SimpleNamespace(
                    scene=scene_file, output=out, seed=6161, hop_ms=250,
                    max_seconds=None, speech_dir=None, stems=True, runner=None,
                    compare_capacity=False, uncalibrated_rt60=False,
                    background_snr_db=target))
                report = json.loads((out / "background_snr.json").read_text())
                ground_truth = json.loads((out / "ground_truth.json").read_text())
                self.assertEqual(ground_truth["background_snr"], report)
                self.assertAlmostEqual(report["measured_voice_to_background_snr_db"],
                                       target, places=7)
                self.assertEqual(report["environment_source_ids"], ["fridge"])
                measured = measured_pcm_snr(out, scene)
                self.assertAlmostEqual(measured, target, delta=0.2,
                                       msg=f"Actual quantized mic0 SNR: {measured}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(CpuBackgroundSnr)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({
            "schema": "libaudition.acoustix-background-snr-cpu.v1",
            "passed": result.wasSuccessful(),
            "real_cpu_rir": True,
            "requested_background_snr_db": [0.0, 6.0, 12.0],
            "pcm_absolute_error_gate_db": 0.2,
            "warning": "Voice versus environmental background only; not speaker SIR",
        }, indent=2) + "\n")
    if not result.wasSuccessful():
        raise SystemExit(1)


if __name__ == "__main__":
    main()
