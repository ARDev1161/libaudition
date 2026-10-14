#!/usr/bin/env python3
"""Contract checks for corpus reference parsing, independent of hardware."""
import importlib.util
import json
import tempfile
from pathlib import Path

source = Path(__file__).with_name("validate_respeaker_corpus.py")
spec = importlib.util.spec_from_file_location("respeaker_validator", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def sample():
    return {"schema": "libaudition.respeaker-corpus.v1", "recordings": [
        {"id": "test01", "wav": "recording.wav", "sha256": "0" * 64,
         "intervals": [{"start_s": 1, "end_s": 2, "azimuth_deg": 30}]}
    ]}


def check_manifest(data, valid):
    with tempfile.TemporaryDirectory() as temp:
        p = Path(temp) / "corpus.json"
        p.write_text(json.dumps(data))
        try:
            result = module.validate_manifest(p)
        except (ValueError, TypeError, KeyError):
            if valid:
                raise
        else:
            if not valid:
                raise AssertionError("Invalid manifest unexpectedly passed")
            assert len(result) == 1


def main():
    check_manifest(sample(), True)
    for mutate in [
        lambda d: d.update(schema="unexpected"),
        lambda d: d["recordings"][0].update(id="../bad"),
        lambda d: d["recordings"][0].update(wav="../escape.wav"),
        lambda d: d["recordings"][0].update(sha256="unverified"),
        lambda d: d["recordings"][0].update(intervals=[]),
        lambda d: d["recordings"][0]["intervals"][0].update(end_s=0),
        lambda d: d["recordings"][0]["intervals"][0].update(azimuth_deg=300),
        lambda d: d["recordings"][0]["intervals"][0].update(azimuth_deg=float("nan")),
    ]:
        d = sample()
        mutate(d)
        check_manifest(d, False)
    intervals = [{"start_s": 0, "end_s": 0.08, "azimuth_deg": 0}]
    obs = [{"hop": str(i), "kind": "track", "x": "1", "y": "0", "z": "0"}
           for i in range(5)]
    segments = module.evaluate(obs, intervals, 10)
    assert len(segments) == 1
    assert segments[0]["median_track_error_deg"] == 0
    assert segments[0]["track_coverage"] == 0.5
    assert segments[0]["track_error_samples"] == 5
    print("respeaker-validator-selftest=ok")


if __name__ == "__main__":
    main()
