#!/usr/bin/env python3
"""Paired native ODAS zero-evidence-birth fix acceptance on identical WAVs.

Do NOT interpret additional active track IDs as speaker/source isolation.
S2 T02/T03/T07 are actual pinned LibriSpeech Acoustix PCM; T00 is
explicitly synthetic zero-PCM with the same sample clock.
"""
import argparse
import json
from pathlib import Path


VARIANTS = ("baseline_off", "baseline_plane",
            "active_var_001_off", "active_var_001_plane")
REAL_CASES = ("t02_dev_single", "t03_eval_two", "t07_eval_seven")
ALL_CASES = ("t00_zero_pcm_control",) + REAL_CASES


class ComparisonError(ValueError):
    pass


def _load(path):
    with Path(path).open(encoding="utf-8") as src:
        return json.load(src)


def _assert_no_zero_strength_births(root, mode, case):
    # Exact native ODAS trace; do not trust only the aggregate counters.
    if mode == "baseline_off":
        trace = (Path(root) / case / "odas" / "sst_raw_trace.jsonl"
                 if case in REAL_CASES else
                 Path(root) / "gmm-sweep/baseline" / case / "sst_raw_trace.jsonl")
    elif mode == "active_var_001_off":
        trace = Path(root) / "gmm-sweep/active_var_001" / case / "sst_raw_trace.jsonl"
    else:
        trace = Path(root) / "horizontal-prior-ab" / mode / case / "sst_raw_trace.jsonl"
    if not trace.is_file():
        raise ComparisonError(f"missing genuine ODAS trace: {trace}")
    rows = []
    with trace.open(encoding="utf-8") as src:
        for index, line in enumerate(src):
            row = json.loads(line)
            if (row["schema"] != "odas_sst_raw_v1"
                    or row["hop_index"] != index
                    or row["sample_offset"] != index * 128
                    or row["odas_timestamp"] != index + 1):
                raise ComparisonError("invalid ODAS replay clock")
            eligible = sum(
                c["strength"] > 0.0 and
                c["birth_posterior"] > row["theta_new"]
                for c in row["ssl_candidates"])
            if len(row["births"]) > eligible:
                raise ComparisonError(
                    f"{mode}/{case}/hop-{index}: source born without positive SSL peak")
            rows.append(row)
    if len(rows) != 1875:
        raise ComparisonError("wrong actual ODAS hop count")
    return {"native_hops_validated": len(rows),
            "births_only_with_positive_ssl_evidence": True}


def compare(before_root, after_root):
    before_root, after_root = Path(before_root), Path(after_root)
    old = _load(before_root / "horizontal-prior-ab/horizontal_prior_report.json")
    new = _load(after_root / "horizontal-prior-ab/horizontal_prior_report.json")
    if (old["schema"] != "libaudition.s2_horizontal_prior_ab_v1" or
            new["schema"] != old["schema"] or
            set(old["variants"]) != set(VARIANTS) or
            set(new["variants"]) != set(VARIANTS)):
        raise ComparisonError("unexpected A/B report schema or variant set")
    result = {
        "schema": "libaudition.zero_ssl_potential_birth_native_ab_v1",
        "comparison": "original pinned instrumented ODAS vs same instrumentation with positive-score birth predicate",
        "quality_claim": "structural birth invariant and track DOA only; NOT speaker identity, SI-SDRi, SIRi, WER or full acoustic acceptance",
        "variants": {},
    }
    for mode in VARIANTS:
        old_variant = old["variants"][mode]
        new_variant = new["variants"][mode]
        if (old_variant["horizontal_sources"] != new_variant["horizontal_sources"]
                or old_variant["gmm"] != new_variant["gmm"]):
            raise ComparisonError("GMM or prior variant not comparable")
        cases = {}
        for case in ALL_CASES:
            a = old_variant["cases"][case]
            b = new_variant["cases"][case]
            if (a["input_wav_sha256"] != b["input_wav_sha256"] or
                    a["parameters"] != b["parameters"] or
                    a["horizontal_source_prior_enabled"] !=
                    b["horizontal_source_prior_enabled"]):
                raise ComparisonError(f"{mode}/{case}: unequal input WAV or options")
            guard = _assert_no_zero_strength_births(after_root, mode, case)
            old_s = a["tracker_metrics"]
            new_s = b["tracker_metrics"]
            if (old_s["validated_128_frame_hops"] != 1875 or
                    new_s["validated_128_frame_hops"] != 1875):
                raise ComparisonError("missing actual audio hops")
            cases[case] = {
                "input_wav_sha256": a["input_wav_sha256"],
                "before": old_s,
                "after": new_s,
                "validated_guard": guard,
            }
            if case == "t03_eval_two":
                old_geo = a["t03_distinct_speaker_3d_matches"]
                new_geo = b["t03_distinct_speaker_3d_matches"]
                if (old_geo["oracle_exactly_two_voice_hops"] != 243 or
                        new_geo["oracle_exactly_two_voice_hops"] != 243):
                    raise ComparisonError("changed genuine T03 oracle overlap")
                cases[case]["before_3d"] = old_geo
                cases[case]["after_3d"] = new_geo
            if case == "t00_zero_pcm_control":
                if new_s["birth_events"] != 0 or new_s["hops_any_active"] != 0:
                    raise ComparisonError(
                        f"{mode}: zero PCM must not birth any SST source")
        if old_variant["gmm"] == "baseline":
            for case in REAL_CASES:
                if cases[case]["before"] != cases[case]["after"]:
                    raise ComparisonError(
                        f"{mode}/{case}: original-GMM tracking unexpectedly changed")
        else:
            # The wide-GMM native experiment MUST regress from its known
            # invalid placeholder-born T00 baseline to zero, on identical PCM.
            if cases["t00_zero_pcm_control"]["before"]["birth_events"] != 1252:
                raise ComparisonError(f"{mode}: original zero-PCM baseline changed")
            if cases["t00_zero_pcm_control"]["after"]["birth_events"] != 0:
                raise ComparisonError("no-signal SST birth regression still present")
        result["variants"][mode] = {"gmm": old_variant["gmm"],
                                    "near_plane": old_variant["horizontal_sources"],
                                    "cases": cases}
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--before", type=Path, required=True)
    p.add_argument("--after", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    comparison = compare(args.before, args.after)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(comparison, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    for mode, result in comparison["variants"].items():
        cases = result["cases"]
        print(json.dumps({
            "variant": mode,
            "t00_births_before": cases["t00_zero_pcm_control"]["before"]["birth_events"],
            "t00_births_after": cases["t00_zero_pcm_control"]["after"]["birth_events"],
            "t02_max_active_before": cases["t02_dev_single"]["before"]["max_simultaneous_active_tracks"],
            "t02_max_active_after": cases["t02_dev_single"]["after"]["max_simultaneous_active_tracks"],
            "t03_two_active_before": cases["t03_eval_two"]["before"]["oracle_two_voice_hops_with_at_least_two_active_tracks"],
            "t03_two_active_after": cases["t03_eval_two"]["after"]["oracle_two_voice_hops_with_at_least_two_active_tracks"],
            "t03_two_correct_doa_after": cases["t03_eval_two"]["after_3d"]["two_distinct_track_to_voice_matches_within_45deg_hops"],
        }, sort_keys=True))


if __name__ == "__main__":
    main()
