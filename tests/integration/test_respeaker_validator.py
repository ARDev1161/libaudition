#!/usr/bin/env python3
"""Contract checks for corpus reference parsing, independent of hardware."""
import importlib.util
import json
import math
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
    # Annotation boundaries must not reset one physical source's track-ID
    # history. Segments are independent DOA windows, not new speaker IDs.
    one_source_split = [
        {"source_id": "speaker", "start_s": 0.0, "end_s": 0.024,
         "azimuth_deg": 0},
        {"source_id": "speaker", "start_s": 0.024, "end_s": 0.048,
         "azimuth_deg": 0},
    ]
    split_manifest = sample()
    split_manifest["recordings"][0]["intervals"] = one_source_split
    check_manifest(split_manifest, True)

    unchanged_id = [dict(hop=i, kind="track", id="10",
                         x="1", y="0", z="0") for i in range(6)]
    stable_segments, stable_quality = module.evaluate(
        unchanged_id, one_source_split, 6, with_summary=True)
    assert len(stable_segments) == 2
    assert all(s["track_coverage"] == 1 for s in stable_segments)
    assert stable_quality["adjacent_hop_id_switches"] == 0

    boundary_switch = [dict(row) for row in unchanged_id]
    for row in boundary_switch[3:]:
        row["id"] = "20"
    split_segments, split_quality = module.evaluate(
        boundary_switch, one_source_split, 6, with_summary=True)
    assert split_segments[0]["adjacent_hop_id_switches"] == 0
    assert split_segments[1]["adjacent_hop_id_switches"] == 1
    assert split_quality["adjacent_hop_id_switches"] == 1
    assert split_quality["id_changes_after_gap"] == 0

    # Across an actual annotation gap, the same source's new track ID is
    # reacquisition, not an adjacent-hop identity switch.
    gapped_source = [
        {"source_id": "speaker", "start_s": 0.0, "end_s": 0.016,
         "azimuth_deg": 0},
        {"source_id": "speaker", "start_s": 0.032, "end_s": 0.048,
         "azimuth_deg": 0},
    ]
    gap_manifest = sample()
    gap_manifest["recordings"][0]["intervals"] = gapped_source
    check_manifest(gap_manifest, True)
    gapped_rows = [dict(hop=i, kind="track", id=str(track_id),
                        x="1", y="0", z="0")
                   for i, track_id in [(0, 10), (1, 10), (4, 20), (5, 20)]]
    gap_segments, gap_quality = module.evaluate(
        gapped_rows, gapped_source, 6, with_summary=True)
    assert gap_segments[1]["id_changes_after_gap"] == 1
    assert gap_quality["adjacent_hop_id_switches"] == 0
    assert gap_quality["id_changes_after_gap"] == 1
    assert gap_quality["track_reacquisitions"] == 1
    assert gap_segments[1]["track_reacquisitions"] == 1

    # The same track ID after a dropout is *still* a reacquisition,
    # but is not an identity change. These are different diagnostics.
    reused_rows = [dict(row) for row in gapped_rows]
    for row in reused_rows:
        row["id"] = "10"
    reused_segments, reused_quality = module.evaluate(
        reused_rows, gapped_source, 6, with_summary=True)
    assert reused_quality["track_reacquisitions"] == 1
    assert reused_quality["id_changes_after_gap"] == 0
    assert reused_quality["adjacent_hop_id_switches"] == 0
    assert reused_segments[0]["track_reacquisitions"] == 0
    assert reused_segments[1]["track_reacquisitions"] == 1

    # Continuous annotation but a missed hop is likewise a reacquisition
    # even if the same ID appears on both sides of the dropout.
    dropout = [dict(hop=i, kind="track", id="10",
                    x="1", y="0", z="0") for i in (0, 1, 3, 4, 5)]
    dropout_metrics, dropout_quality = module.evaluate(
        dropout, [{"source_id": "speaker", "start_s": 0.0,
                   "end_s": 0.048, "azimuth_deg": 0}], 6, with_summary=True)
    assert dropout_quality["track_reacquisitions"] == 1
    assert dropout_quality["id_changes_after_gap"] == 0
    assert dropout_metrics[0]["track_coverage"] == 5 / 6

    # Ground-truth quantitative acceptance covers overlapping sources and
    # otherwise silent hops, not just median DOA of whichever tracks exist.
    from types import SimpleNamespace
    def gates(**overrides):
        defaults = dict(max_median_error_deg=10.0, max_p90_error_deg=20.0,
                        min_track_coverage=1.0, max_adjacent_id_switches=0,
                        min_simultaneous_track_coverage=1.0,
                        max_silent_false_positive_fraction=0.0,
                        max_active_unmatched_track_fraction=None,
                        max_adjacent_cross_source_id_handoffs=None)
        defaults.update(overrides)
        return SimpleNamespace(**defaults)

    two_sources = [
        {"source_id": "a", "start_s": 0.0, "end_s": 0.048, "azimuth_deg": 0},
        {"source_id": "b", "start_s": 0.016, "end_s": 0.048, "azimuth_deg": 90},
    ]
    def trace(hop, ident, x=1.0, y=0.0):
        return {"hop": str(hop), "kind": "track", "id": str(ident),
                "x": str(x), "y": str(y), "z": "0"}
    clean = ([trace(i, 10) for i in range(6)] +
             [trace(i, 20, 0, 1) for i in range(2, 6)])
    metrics, quality = module.evaluate(clean, two_sources, 8, 30, with_summary=True)
    assert len(metrics) == 2
    assert all(m["track_coverage"] == 1 for m in metrics)
    assert all(m["p90_track_error_deg"] == 0 for m in metrics)
    assert quality["active_simultaneous_hops"] == 4
    assert quality["simultaneous_track_coverage"] == 1
    assert quality["silent_hops"] == 2
    assert quality["silent_false_positive_hop_fraction"] == 0
    assert quality["cross_source_id_handoffs"] == 0
    assert quality["cross_source_id_reuse_after_gap"] == 0
    module.enforce_thresholds(metrics, quality, gates())

    def fails(segments, summary, options):
        try:
            module.enforce_thresholds(segments, summary, options)
        except ValueError:
            return
        raise AssertionError("Invalid quality result unexpectedly passed acceptance")

    # ID ownership can change between *different* annotated sources
    # without triggering either source's own ID-switch counter. Detect
    # handoffs across adjacent hops even when both sources were localized
    # perfectly and they did not overlap in time.
    sequential_sources = [
        {"source_id": "a", "start_s": 0.0, "end_s": 0.024,
         "azimuth_deg": 0},
        {"source_id": "b", "start_s": 0.024, "end_s": 0.048,
         "azimuth_deg": 90},
    ]
    sequential_manifest = sample()
    sequential_manifest["recordings"][0]["intervals"] = sequential_sources
    check_manifest(sequential_manifest, True)
    adjacent_rows = ([trace(i, 41) for i in range(3)] +
                     [trace(i, 41, 0, 1) for i in range(3, 6)])
    transition_segments, transition_quality = module.evaluate(
        adjacent_rows, sequential_sources, 8, 30, with_summary=True)
    assert all(s["track_coverage"] == 1 for s in transition_segments)
    assert transition_quality["adjacent_hop_id_switches"] == 0
    assert transition_quality["track_reacquisitions"] == 0
    assert transition_quality["cross_source_id_handoffs"] == 1
    assert transition_quality["cross_source_id_reuse_after_gap"] == 0
    assert transition_segments[0]["cross_source_id_handoffs"] == 0
    assert transition_segments[1]["cross_source_id_handoffs"] == 1
    baseline = gates(min_simultaneous_track_coverage=None)
    module.enforce_thresholds(transition_segments, transition_quality, baseline)
    fails(transition_segments, transition_quality,
          gates(min_simultaneous_track_coverage=None,
                max_adjacent_cross_source_id_handoffs=0))
    module.enforce_thresholds(
        transition_segments, transition_quality,
        gates(min_simultaneous_track_coverage=None,
              max_adjacent_cross_source_id_handoffs=1))

    # Recycling the same integer ID after multiple unmatched hops is
    # different from an adjacent source handoff and may be legitimate.
    delayed_sources = [
        sequential_sources[0],
        {"source_id": "b", "start_s": 0.056, "end_s": 0.08,
         "azimuth_deg": 90},
    ]
    delayed_manifest = sample()
    delayed_manifest["recordings"][0]["intervals"] = delayed_sources
    check_manifest(delayed_manifest, True)
    delayed_rows = ([trace(i, 41) for i in range(3)] +
                    [trace(i, 41, 0, 1) for i in range(7, 10)])
    delayed_segments, delayed_quality = module.evaluate(
        delayed_rows, delayed_sources, 12, 30, with_summary=True)
    assert all(s["track_coverage"] == 1 for s in delayed_segments)
    assert delayed_quality["cross_source_id_handoffs"] == 0
    assert delayed_quality["cross_source_id_reuse_after_gap"] == 1
    assert delayed_segments[1]["cross_source_id_reuse_after_gap"] == 1
    assert delayed_quality["id_changes_after_gap"] == 0
    module.enforce_thresholds(
        delayed_segments, delayed_quality,
        gates(min_simultaneous_track_coverage=None,
              max_adjacent_cross_source_id_handoffs=0))

    # Simultaneous sources exchanging track IDs cause *two* consecutive
    # cross-source handoffs; the older per-source switch gate still fires.
    simultaneous_swap = [dict(row) for row in clean]
    for row in simultaneous_swap:
        if row["hop"] == "5":
            row["id"] = "20" if row["id"] == "10" else "10"
    swapped_segments, swapped_quality = module.evaluate(
        simultaneous_swap, two_sources, 8, 30, with_summary=True)
    assert swapped_quality["adjacent_hop_id_switches"] == 2
    assert swapped_quality["cross_source_id_handoffs"] == 2
    assert swapped_quality["cross_source_id_reuse_after_gap"] == 0
    fails(swapped_segments, swapped_quality,
          gates(max_adjacent_id_switches=2,
                max_adjacent_cross_source_id_handoffs=0))
    module.enforce_thresholds(
        swapped_segments, swapped_quality,
        gates(max_adjacent_id_switches=2,
              max_adjacent_cross_source_id_handoffs=2))

    noisy = clean + [trace(6, 99)]
    metrics, quality = module.evaluate(noisy, two_sources, 8, 30, with_summary=True)
    assert quality["silent_hops_with_tracks"] == 1
    assert quality["silent_false_positive_hop_fraction"] == 0.5
    fails(metrics, quality, gates())
    module.enforce_thresholds(
        metrics, quality, gates(max_silent_false_positive_fraction=0.5))

    # A correct source with extra ghost tracks in *active* hops passed all
    # former DOA/coverage/silence gates. Score those ghosts independently.
    ghost_rows = clean + [trace(0, 99, 0, 1), trace(3, 99, -1, 0)]
    ghost_segments, ghost_quality = module.evaluate(
        ghost_rows, two_sources, 8, 30, with_summary=True)
    assert all(s["track_coverage"] == 1 for s in ghost_segments)
    assert ghost_quality["silent_false_positive_hop_fraction"] == 0
    assert ghost_quality["active_hops"] == 6
    assert ghost_quality["active_hops_with_unmatched_tracks"] == 2
    assert ghost_quality["active_unmatched_track_observations"] == 2
    assert ghost_quality["active_unmatched_track_hop_fraction"] == 2 / 6
    # The old gates still pass because the new limit is opt-in until
    # its threshold is calibrated from real recordings.
    module.enforce_thresholds(ghost_segments, ghost_quality, gates())
    fails(ghost_segments, ghost_quality,
          gates(max_active_unmatched_track_fraction=0.0))
    module.enforce_thresholds(
        ghost_segments, ghost_quality,
        gates(max_active_unmatched_track_fraction=2 / 6))
    assert quality["active_unmatched_track_hop_fraction"] == 0

    switched = [dict(row) for row in clean]
    for row in switched:
        if row["id"] == "10" and row["hop"] == "5":
            row["id"] = "30"
    metrics, quality = module.evaluate(switched, two_sources, 8, 30, with_summary=True)
    assert quality["adjacent_hop_id_switches"] == 1
    fails(metrics, quality, gates())
    module.enforce_thresholds(metrics, quality, gates(max_adjacent_id_switches=1))


    # Identical track IDs cannot be scored as two independently resolved
    # sources in the same hop, even with two different unit directions.
    duplicated_ids = clean + [trace(2, 10, 0, 1)]
    try:
        module.evaluate(duplicated_ids, two_sources, 8, 30)
    except ValueError as error:
        assert "Duplicate track ID" in str(error)
    else:
        raise AssertionError("Duplicate per-hop track ID inflated coverage")
    # The same track ID in a later hop is normal tracking, not duplication.
    assert module.evaluate([trace(0, 10), trace(1, 10)],
                           two_sources[:1], 8)[0]["track_coverage"] == 2 / 6

    # The real-corpus gate must not trust a cropped or forged hop count.
    # A truncated CSV must not hide silent-hop false positives either.
    valid_summary = {
        "schema": "libaudition.offline-integration.v1",
        "input_frames": 8 * module.HOP, "sample_rate_hz": module.FS,
        "hops": 8, "track_observations": len(clean),
        "potential_observations": 0, "recorder_drops": 0,
    }
    module.validate_runner_trace(valid_summary, 8 * module.HOP, clean)
    def bad_trace(summary, rows=clean):
        try:
            module.validate_runner_trace(summary, 8 * module.HOP, rows)
        except ValueError:
            return
        raise AssertionError("Corrupt ODAS summary/trace accepted")
    for change in (
        {"hops": 6},
        {"hops": 9},
        {"input_frames": 7 * module.HOP},
        {"sample_rate_hz": 8000},
        {"track_observations": len(clean) - 1},
        {"potential_observations": 1},
        {"recorder_drops": 1},
        {"hops": True},
        {"schema": "unrecognized"},
    ):
        bad_trace({**valid_summary, **change})
    bad_trace(valid_summary, clean[:-1])


    # Two distinct moving sources meet at exactly 0 degrees on hop 2.
    # The runner can emit their rows in either order. At identical
    # directional cost, continuity of the previous ID should decide,
    # not CSV ordering. Once trajectories diverge, geometry takes priority.
    crossing = [
        {"source_id": "left_to_right", "start_s": 0.0, "end_s": 0.04,
         "azimuth_start_deg": -60.0, "azimuth_end_deg": 60.0},
        {"source_id": "right_to_left", "start_s": 0.0, "end_s": 0.04,
         "azimuth_start_deg": 60.0, "azimuth_end_deg": -60.0},
    ]
    crossing_manifest = sample()
    crossing_manifest["recordings"][0]["intervals"] = crossing
    check_manifest(crossing_manifest, True)

    def crossing_row(hop, ident, source):
        at = (hop + 0.5) * module.HOP / module.FS
        angle = math.radians(module.reference_azimuth(crossing[source], at))
        return trace(hop, ident, math.cos(angle), math.sin(angle))

    crossing_rows = []
    for hop in range(5):
        pair = [crossing_row(hop, 10, 0), crossing_row(hop, 20, 1)]
        if hop == 2:
            pair.reverse()  # Real output order may vary at a crossing.
        crossing_rows.extend(pair)
    # Retain two genuine silent hops: quality acceptance must test its
    # false-positive threshold rather than failing due to absent evidence.
    crossing_metrics, crossing_quality = module.evaluate(
        crossing_rows, crossing, 7, 20, with_summary=True)
    assert all(s["track_coverage"] == 1 for s in crossing_metrics)
    assert crossing_quality["simultaneous_track_coverage"] == 1
    assert crossing_quality["adjacent_hop_id_switches"] == 0
    assert crossing_quality["id_changes_after_gap"] == 0
    module.enforce_thresholds(crossing_metrics, crossing_quality, gates())

    # An actual ID swap after crossing must NOT be hidden by continuity:
    # exact directional matching outranks the prior-hop ID preference.
    actual_swap = [dict(row) for row in crossing_rows]
    for row in actual_swap:
        if int(row["hop"]) >= 3:
            row["id"] = "20" if row["id"] == "10" else "10"
    swapped_metrics, swapped_quality = module.evaluate(
        actual_swap, crossing, 7, 20, with_summary=True)
    assert all(s["track_coverage"] == 1 for s in swapped_metrics)
    assert swapped_quality["adjacent_hop_id_switches"] == 2
    fails(swapped_metrics, swapped_quality, gates())
    module.enforce_thresholds(
        swapped_metrics, swapped_quality, gates(max_adjacent_id_switches=2))

    # Direct assignment contract: geometric error still overrides
    # preferred IDs, and a missing history leaves the old API usable.
    opposite = [crossing_row(0, 20, 0), crossing_row(0, 10, 1)]
    expected = [module.reference_direction(src, 0.004) for src in crossing]
    geometric = module.associate_with_indices(
        expected, opposite, 20, preferred_ids=[10, 20])
    assert [opposite[idx]["id"] for _, idx, _ in geometric] == ["20", "10"]
    assert len(module.associate_with_indices(expected, opposite)) == 2

    # A 60-degree outlier leaves the median at zero; P90 must catch it.
    outlier = [dict(row) for row in clean]
    outlier[0] = trace(0, 10, 0.5, 0.8660254037844386)
    metrics, quality = module.evaluate(outlier, two_sources, 8, 180, with_summary=True)
    assert metrics[0]["median_track_error_deg"] == 0
    assert 59 < metrics[0]["p90_track_error_deg"] < 61
    fails(metrics, quality, gates())
    module.enforce_thresholds(metrics, quality, gates(max_p90_error_deg=65))

    # Angle gates reject wrong-direction matches instead of counting a track
    # as coverage merely because a track exists at that hop.
    metrics, quality = module.evaluate(outlier, two_sources, 8, 30, with_summary=True)
    assert metrics[0]["track_coverage"] == 5 / 6
    fails(metrics, quality, gates())
    module.enforce_thresholds(metrics, quality, gates(min_track_coverage=0.8))

    # Missing applicable evidence must fail closed, not vacuously pass.
    metrics, quality = module.evaluate(clean[:6], two_sources[:1], 6,
                                       with_summary=True)
    assert quality["simultaneous_track_coverage"] is None
    assert quality["silent_false_positive_hop_fraction"] is None
    fails(metrics, quality, gates(min_simultaneous_track_coverage=0.5))
    fails(metrics, quality, gates(min_simultaneous_track_coverage=None))

    for coords in [(0, 0, 0), (2, 0, 0), (float("nan"), 0, 0)]:
        invalid_row = [trace(0, 1, coords[0], coords[1])]
        invalid_row[0]["z"] = str(coords[2])
        try:
            module.evaluate(invalid_row, two_sources[:1], 8)
        except ValueError:
            pass
        else:
            raise AssertionError("Non-unit/nonfinite trace direction accepted")

    # One predicted source can satisfy only one of two ground-truth sources.
    single = [trace(i, 10) for i in range(6)]
    metrics, quality = module.evaluate(single, two_sources, 8, 30,
                                       with_summary=True)
    assert quality["simultaneous_track_coverage"] == 0
    fails(metrics, quality, gates(min_track_coverage=0.0))

    # LOCATA ground truth is genuinely 3D: elevation must not disappear when
    # a legacy horizontal azimuth-only trace is scored.
    full_3d = sample()
    full_3d["recordings"][0]["intervals"] = [
        {"source_id": "high", "start_s": 0, "end_s": 0.08,
         "direction_xyz": [0.6, 0.0, 0.8]}]
    check_manifest(full_3d, True)
    vertical = [{"hop": str(i), "kind": "track", "id": "19",
                 "x": "0.6", "y": "0", "z": "0.8"} for i in range(10)]
    elevated = module.evaluate(vertical, full_3d["recordings"][0]["intervals"],
                               10, max_association_error_deg=20)[0]
    assert elevated["track_coverage"] == 1
    assert abs(elevated["median_track_error_deg"]) < 1e-5
    horizontal = [{"hop": str(i), "kind": "track", "id": "19",
                   "x": "1", "y": "0", "z": "0"} for i in range(10)]
    misses = module.evaluate(horizontal, full_3d["recordings"][0]["intervals"],
                             10, max_association_error_deg=20)[0]
    assert misses["track_coverage"] == 0
    for vector in ([0, 0, 0], [1, 1, 0], [float("nan"), 0, 1]):
        invalid_3d = sample()
        invalid_3d["recordings"][0]["intervals"] = [
            {"start_s": 0, "end_s": 0.08, "direction_xyz": vector}]
        check_manifest(invalid_3d, False)
    conflicting_3d = sample()
    conflicting_3d["recordings"][0]["intervals"] = [
        {"start_s": 0, "end_s": 0.08,
         "azimuth_deg": 0, "direction_xyz": [1, 0, 0]}]
    check_manifest(conflicting_3d, False)

    # Corpus importer: exact WAV bytes, checksum, manifest, duplicate rejection.
    import wave
    import hashlib
    importer_source = Path(__file__).with_name("import_respeaker_recording.py")
    importer_spec = importlib.util.spec_from_file_location(
        "respeaker_importer", importer_source)
    importer = importlib.util.module_from_spec(importer_spec)
    importer_spec.loader.exec_module(importer)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        wav = root / "capture.wav"
        with wave.open(str(wav), "wb") as output:
            output.setnchannels(6)
            output.setsampwidth(2)
            output.setframerate(16000)
            output.writeframes(bytes(128 * 6 * 2 * 20))
        annotation = root / "annotation.json"
        annotation.write_text(json.dumps({
            "id": "verified_clip",
            "intervals": [{"start_s": 0, "end_s": 0.08,
                           "azimuth_deg": 30}]}))
        target = root / "dataset"
        importer.import_recording(wav, annotation, target)
        assert (target / "verified_clip.wav").read_bytes() == wav.read_bytes()
        assert len(module.validate_manifest(target / "corpus.json")) == 1
        try:
            importer.import_recording(wav, annotation, target)
        except ValueError:
            pass
        else:
            raise AssertionError("Duplicate recording was accepted")
    # Geometry-neutral import: no ReSpeaker layout, 4-channel example with
    # externally measured coordinates. Never derive directions from audio.
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        four_channel = root / "four-channel.wav"
        with wave.open(str(four_channel), "wb") as output:
            output.setnchannels(4)
            output.setsampwidth(2)
            output.setframerate(16000)
            output.writeframes(bytes(128 * 4 * 2 * 20))
        array = {"microphones": [
            {"channel": 0, "xyz_m": [-0.04, 0.0, 0.0]},
            {"channel": 1, "xyz_m": [0.04, 0.0, 0.0]},
            {"channel": 2, "xyz_m": [0.0, -0.04, 0.0]},
            {"channel": 3, "xyz_m": [0.0, 0.04, 0.0]},
        ]}
        target = root / "generic-array"
        annotation = root / "ground-truth.json"

        def import_named(clip_id, geometry=array):
            annotation.write_text(json.dumps({
                "id": clip_id, "intervals": [
                    {"source_id": "reference", "start_s": 0,
                     "end_s": 0.08, "azimuth_deg": 30}]}))
            importer.import_recording(
                four_channel, annotation, target, geometry)

        import_named("array_a")
        import_named("array_b")
        stored = json.loads((target / "corpus.json").read_text())
        assert stored["array"] == array
        assert [x["id"] for x in stored["recordings"]] == ["array_a", "array_b"]
        assert len(module.validate_manifest(target / "corpus.json")) == 2
        for rec in stored["recordings"]:
            imported_audio = target / rec["wav"]
            assert imported_audio.read_bytes() == four_channel.read_bytes()
            assert hashlib.sha256(imported_audio.read_bytes()).hexdigest() == rec["sha256"]

        # No geometry may be inferred from a four-channel waveform.
        annotation.write_text(json.dumps({
            "id": "wrong_implicit", "intervals": [
                {"start_s": 0, "end_s": 0.08, "azimuth_deg": 0}]}))
        for invalid_geometry in (
            None,
            {"microphones": [
                {"channel": 0, "xyz_m": [0, 0, 0]},
                {"channel": 5, "xyz_m": [1, 0, 0]}]},
            {"microphones": [
                {"channel": 0, "xyz_m": [0, 0, 0]},
                {"channel": 0, "xyz_m": [1, 0, 0]}]},
            {"microphones": [
                {"channel": 0, "xyz_m": [-0.05, 0.0, 0.0]},
                {"channel": 1, "xyz_m": [0.04, 0.0, 0.0]},
                {"channel": 2, "xyz_m": [0.0, -0.04, 0.0]},
                {"channel": 3, "xyz_m": [0.0, 0.04, 0.0]}]},

            {"microphones": [
                {"channel": 0, "xyz_m": [0, 0, 0]},
                {"channel": 1, "xyz_m": [float("nan"), 0, 0]}]},
        ):
            before = (target / "corpus.json").read_bytes()
            try:
                importer.import_recording(
                    four_channel, annotation, target, invalid_geometry)
            except (ValueError, TypeError):
                pass
            else:
                raise AssertionError("Invalid or inconsistent geometry accepted")
            assert (target / "corpus.json").read_bytes() == before
            assert not (target / "wrong_implicit.wav").exists()

        # An already-imported ID must never overwrite a WAV or its manifest.
        before = (target / "corpus.json").read_bytes()
        try:
            import_named("array_a")
        except ValueError:
            pass
        else:
            raise AssertionError("Duplicate real recording ID accepted")
        assert (target / "corpus.json").read_bytes() == before

        # The WAV chunk header may advertise more PCM than actually exists.
        truncated = root / "truncated.wav"
        truncated.write_bytes(four_channel.read_bytes()[:-8])
        annotation.write_text(json.dumps({
            "id": "truncated", "intervals": [
                {"start_s": 0, "end_s": 0.08, "azimuth_deg": 0}]}))
        try:
            importer.import_recording(truncated, annotation, target, array)
        except ValueError:
            pass
        else:
            raise AssertionError("Truncated real WAV accepted")
        assert not (target / "truncated.wav").exists()

    print("respeaker-validator-selftest=ok")


if __name__ == "__main__":
    main()
