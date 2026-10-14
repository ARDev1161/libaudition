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
    obs = [{"hop": str(i), "kind": "track", "id": "7", "x": "1", "y": "0", "z": "0"}
           for i in range(5)]
    segments = module.evaluate(obs, intervals, 10)
    assert len(segments) == 1
    assert segments[0]["median_track_error_deg"] == 0
    assert segments[0]["track_coverage"] == 0.5
    assert segments[0]["track_error_samples"] == 5
    moving = sample()
    moving["recordings"][0]["intervals"] = [
        {"source_id": "a", "start_s": 0, "end_s": 0.08,
         "azimuth_start_deg": -30, "azimuth_end_deg": 30},
        {"source_id": "b", "start_s": 0, "end_s": 0.08,
         "azimuth_deg": 90},
    ]
    check_manifest(moving, True)
    assert module.reference_azimuth(moving["recordings"][0]["intervals"][0],
                                    0.04) == 0
    assert len(module.associate([0, 90], [
        {"x": "1", "y": "0", "z": "0"},
        {"x": "0", "y": "1", "z": "0"}])) == 2
    assert len(module.associate([0, 90], [
        {"x": "1", "y": "0", "z": "0"}])) == 1
    two_rows = [{"hop": str(i), "kind": "track", "id": "7", "x": "1", "y": "0", "z": "0"}
                for i in range(10)]
    scores = module.evaluate(two_rows, [
        {"source_id": "a", "start_s": 0, "end_s": 0.08, "azimuth_deg": 0},
        {"source_id": "b", "start_s": 0, "end_s": 0.08, "azimuth_deg": 90},
    ], 10)
    assert sum(s["track_coverage"] for s in scores) == 1
    # A single track must not count as coverage for both simultaneous sources.
    bad = sample()
    bad["recordings"][0]["intervals"] = [
        {"source_id": "a", "start_s": 0, "end_s": 0.08, "azimuth_deg": 0},
        {"source_id": "a", "start_s": 0.04, "end_s": 0.12, "azimuth_deg": 90},
    ]
    check_manifest(bad, False)
    source = [{"source_id": "speaker", "start_s": 0, "end_s": 0.08,
               "azimuth_deg": 0}]
    stable = [{"hop": str(i), "kind": "track", "id": "1",
               "x": "1", "y": "0", "z": "0"} for i in range(10)]
    result = module.evaluate(stable, source, 10)[0]
    assert result["adjacent_hop_id_switches"] == 0
    assert result["id_changes_after_gap"] == 0
    switched = [dict(x) for x in stable]
    for x in switched[5:]:
        x["id"] = "2"
    result = module.evaluate(switched, source, 10)[0]
    assert result["adjacent_hop_id_switches"] == 1
    assert result["id_changes_after_gap"] == 0
    interrupted = [x for x in switched if x["hop"] != "5"]
    result = module.evaluate(interrupted, source, 10)[0]
    assert result["adjacent_hop_id_switches"] == 0
    assert result["id_changes_after_gap"] == 1
    print("respeaker-validator-selftest=ok")


if __name__ == "__main__":
    main()
