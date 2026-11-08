#!/usr/bin/env python3
"""Audit actual ODAS per-candidate GMM likelihood and epsilon-floor dominance.

Real PCM and source directions are NOT inferred from this script; the input
is the recorded native ODAS SSL/SST JSONL from immutable Acoustix S2 replay.
This is diagnosis, NOT a safe default for changing tracker thresholds.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


CASES = ("t02_dev_single", "t03_eval_two", "t07_eval_seven")
# Frozen actual libaudition offline runner OdasOptions; explicitly verify
# these C++ defaults in CI. Both GMMs are single 1D normal components.
ACTIVE_MEAN = 0.4
INACTIVE_MEAN = 0.25
VARIANCE = 0.0025
P_NEW = 0.1
P_FALSE = 0.1
EPSILON = 1.0e-20


class AuditError(ValueError):
    pass


def gaussian_log_odds(strength):
    """log [P(new)*active_GMM(s) / (P(false)*inactive_GMM(s))].

    The native ODAS diffuse factor cancels between labels. For equal
    Gaussian variance and priors, this simplifies exactly to 60*s-19.5.
    A track-association class affects normalisation but NOT these odds.
    """
    if type(strength) not in (float, int) or not math.isfinite(strength):
        raise AuditError("non-finite SSL candidate strength")
    if not 0.0 <= strength <= 1.0:
        raise AuditError("SSL strength must be within [0, 1]")
    return (math.log(P_NEW / P_FALSE)
            + ((strength - INACTIVE_MEAN) ** 2
               - (strength - ACTIVE_MEAN) ** 2) / (2.0 * VARIANCE))


def decompose_candidate(candidate):
    """Estimate false/epsilon split of recorded unassigned posterior.

    The JSONL records P_new and P_each_existing_track. Posterior for the
    untracked class is the complement. ODAS has a false-peak label plus
    local absolute epsilon, which we separate analytically using GMM odds.
    Epsilon is computed from a FLOAT-ROUNDED residual, so we report it as
    an estimate and only classify dominance well away from boundaries.
    """
    s = candidate["strength"]
    ln = gaussian_log_odds(s)
    birth = candidate["birth_posterior"]
    tracks = candidate["track_posteriors"]
    if type(birth) not in (int, float) or not math.isfinite(birth) or not 0 <= birth <= 1:
        raise AuditError("invalid ODAS birth posterior")
    if type(tracks) is not list or len(tracks) > 32:
        raise AuditError("invalid bounded SST existing-track array")
    total_track = 0.0
    seen = set()
    for tr in tracks:
        tid, posterior = tr["track_id"], tr["posterior"]
        if type(tid) is not int or tid <= 0 or tid in seen:
            raise AuditError("invalid/duplicate existing-track identity")
        seen.add(tid)
        if (type(posterior) not in (float, int) or not math.isfinite(posterior)
                or not 0 <= posterior <= 1):
            raise AuditError("invalid ODAS track posterior")
        total_track += posterior
    residual = 1.0 - birth - total_track
    if residual < -0.0001 or residual > 1.0001:
        raise AuditError("ODAS posterior mass not normalized")
    if not -100.0 <= ln <= 100.0:
        raise AuditError("unexpected GMM log-odds range")
    inferred_false = birth * math.exp(-ln)
    if inferred_false > residual + 0.0001:
        raise AuditError("GMM-predicted false posterior exceeds residual")
    epsilon_estimate = max(0.0, residual - inferred_false)
    # Native float32 marginals are printed to ~9 digits; do not claim
    # exact decomposition at 1e-7 precision.
    return {"strength": s, "birth": birth, "tracks": total_track,
            "residual": max(0.0, residual),
            "inferred_false": inferred_false,
            "inferred_epsilon": epsilon_estimate,
            "log_birth_false_odds": ln}


def summarize_case(records, frames):
    if frames <= 0 or frames % 128 or len(records) != frames // 128:
        raise AuditError("WAV/ODAS hop count mismatch")
    out = {"hops": len(records), "candidates": 0, "epsilon_dominant": 0,
           "epsilon_dominant_hops": 0,
           "strong_ssl_gt_0_8": 0,
           "strong_ssl_gt_0_8_epsilon_dominant": 0,
           "strong_ssl_gt_0_8_birth_lt_0_01": 0,
           "gmm_equal_odds_strength": 0.325,
           "no_existing_tracks_birth_0_9_strength_lower_bound":
               round((19.5 + math.log(9.0)) / 60.0, 8),
           "epsilon_dominant_examples": []}
    examples = []
    for hop, record in enumerate(records):
        if (record["schema"] != "odas_sst_raw_v1"
                or record["odas_timestamp"] != hop + 1
                or record["hop_index"] != hop
                or record["hop_frames"] != 128
                or record["sample_offset"] != 128 * hop):
            raise AuditError("not contiguous actual ODAS 128-frame timeline")
        candidates = record["ssl_candidates"]
        if type(candidates) is not list or len(candidates) > 128:
            raise AuditError("invalid SSL candidate array")
        dominated_this_hop = False
        for index, candidate in enumerate(candidates):
            if candidate["index"] != index:
                raise AuditError("inconsistent SSL candidate index")
            t = decompose_candidate(candidate)
            out["candidates"] += 1
            if t["strength"] > 0.8:
                out["strong_ssl_gt_0_8"] += 1
                if t["birth"] < 0.01:
                    out["strong_ssl_gt_0_8_birth_lt_0_01"] += 1
            if t["inferred_epsilon"] > 0.5:
                dominated_this_hop = True
                out["epsilon_dominant"] += 1
                if t["strength"] > 0.8:
                    out["strong_ssl_gt_0_8_epsilon_dominant"] += 1
                examples.append({
                    "hop_index": hop, "candidate_index": index,
                    "strength": round(t["strength"], 8),
                    "birth_posterior": round(t["birth"], 12),
                    "existing_track_mass": round(t["tracks"], 8),
                    "inferred_epsilon_mass": round(t["inferred_epsilon"], 8)})
        out["epsilon_dominant_hops"] += dominated_this_hop
    out["epsilon_dominant_examples"] = examples[:8]
    return out


def audit_artifact(root):
    results = {}
    for case_name in CASES:
        case = Path(root) / case_name
        wav = case / "render/input.wav"
        checksum = hashlib.sha256(wav.read_bytes()).hexdigest()
        if checksum != (case / "input_sha256.txt").read_text().strip():
            raise AuditError(f"{case_name}: frozen PCM hash mismatch")
        summary = json.loads((case / "odas/summary.json").read_text())
        if summary["recorder_drops"] != 0:
            raise AuditError(f"{case_name}: dropped audio blocks")
        with (case / "odas/sst_raw_trace.jsonl").open(encoding="utf-8") as src:
            rows = [json.loads(line) for line in src]
        results[case_name] = {
            "input_pcm_sha256": checksum,
            **summarize_case(rows, summary["input_frames"])}
    return {
        "schema": "libaudition.acoustix-s2-native-gmm-epsilon-evidence.v1",
        "fixed_odas_gmm": {
            "active_mean": ACTIVE_MEAN, "inactive_mean": INACTIVE_MEAN,
            "variance": VARIANCE, "false_prior": P_FALSE,
            "new_prior": P_NEW, "epsilon": EPSILON},
        "interpretation": "actual ODAS model label evidence only; NOT audio "
                          "separation, speaker recognition, ASR, or DSP quality",
        "cases": results,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--artifact-root", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    report = audit_artifact(args.artifact_root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({n: {k: v for k, v in report["cases"][n].items()
                          if k in ("hops", "candidates", "epsilon_dominant",
                                   "strong_ssl_gt_0_8",
                                   "strong_ssl_gt_0_8_birth_lt_0_01")}
                      for n in CASES}, sort_keys=True))


if __name__ == "__main__":
    main()
