# O2: near-horizontal source-prior ablation on real Acoustix S2 WAVs

**Status: diagnostic-only experiment.** This is NOT a localization,
source-separation, identity or ASR algorithm fix. The experiment
changes only the offline test runner input option; production
libaudition and ODAS remain unchanged. Only the frozen six-channel
real-LibriSpeech Acoustix PCM is used for T02/T03/T07.

## Falsifiable hypothesis

The robot uses a planar four-microphone spatial array. In the frozen
Acoustix S2 scenes, oracle voice elevation relative to the robot is
approximately 8.13 degrees absolute for T02; T03 covers about
3.53-8.71 degrees, and T07 roughly 2.79-9.17 degrees. Source
elevation from a planar array is weakly identifiable, particularly
under reverberation and broad/vertical spatial-filter response.

Prior experiment (real native Acoustix action 42442794479, SHA
b7e0c2d10a1f2b49cdd46d6feecf9e4f91ab303a) showed that
raising active SST GMM variance from 0.0025 to 0.01 creates
two simultaneous active tracks in 227/243 two-voice oracle hops.
**NONE** of those hops has one-to-one distinct-track/different
speaker 3D DOA matches even within 45 degrees. The median best
possible *worst* track-to-oracle error is ~81.83 degrees.
A recurring extra track points steeply upward, sometimes with
absolute z ~0.915. T02 fragments into six active IDs and T00
digital zero audio produces 1252 preliminary births.

We will test whether the *existing*, explicitly opt-in
applyOdasNearPlaneSourcePrior() mode removes those vertical
phantoms. Because all evaluated human sources are near horizontal
in this frozen scene, it is a reasonable **scene-specific
assumption**, not a globally safe source prior: real robots must
also hear above and below (alarms, persons on stairs, sound from
elevated speakers, etc.).

## Four-variant, two-factor A/B matrix

| Variant | SST active GMM variance | Existing near-plane prior |
| --- | ---: | --- |
| baseline_off | 0.0025 | no |
| baseline_plane | 0.0025 | yes |
| active_var_001_off | 0.01 | no |
| active_var_001_plane | 0.01 | yes |

Everything else stays fixed: GMM means/weights, Pfalse/Pnew/Ptrack,
theta_new=0.9, five-hop probation/threshold 0.8, four SST slots,
mic coordinates, original PCM and source oracle. This is not a
parameter optimization on held-out quality; it is a local causal
ablation to test the source-height hypothesis.

Existing no-prior variants are re-read from the same CI run's native
GMM matrix; prior-enabled variants each reprocess the same WAV
through the pinned C++ ODAS SSL/SST/SSS runner using
--horizontal-sources, and optionally --sst-active-variance 0.01.

All four replays include:
- T02 genuine one-human speech;
- T03 genuine two-human LibriSpeech speech, 243 overlap oracle hops;
- T07 genuine seven human voices plus other recorded/rendered noise;
- T00 expressly SYNTHETIC all-zero six-channel PCM, not real ambient
  sound, never a substitute for the room-noise acceptance scenario.

The native runner now persists `horizontal_sources` as a boolean
in summary.json so the audit can verify that the requested
source-direction prior actually took effect. An immutable WAV SHA,
128-frame ODAS clock, validated native JSONL, zero drops and exact
runtime GMM parameters are required for every case.

## What counts as evidence

The script `tests/integration/acoustix_s2_horizontal_prior_ab.py`
measures every active tracked source's absolute 3D elevation and
counts active-track observations above 45 degrees. It also uses
exact one-to-one pairing of **two different active SST IDs** and
**two different true oracle people**, at strict 25 and generous
45 degrees. The latter is a geometric diagnostic only, NOT
speaker separation or recognition.

It reports the T03 two-active-track count and distinct two-oracle
match count *separately*; any extra track that points up or to a
reflection cannot count as recognizing the second human. T02
track fragmentation and T00 provisional births are independently
reported so a restrictive prior does not mask other problems.

Run inside the exact pinned Acoustix job after its normal 4-variant
GMM native sweep:

    python3 -m unittest discover -s tests/integration -p 'test_acoustix_s2_horizontal_prior_ab.py' -v
    python3 tests/integration/acoustix_s2_horizontal_prior_ab.py --artifact-root build-s2 --runner build-s2-odas/tests/audition_offline_integration --validator <exact-pinned-ODAS>/tools/validate_sst_raw_trace.py --output build-s2/horizontal-prior-ab

The report is `build-s2/horizontal-prior-ab/horizontal_prior_report.json`;
it and the per-variant raw JSONLs are included in the GitHub artifact.
The script refuses stale output paths, fake source SHA, missing
native validator, invalid WAV/trace clocks, impossible track
direction vectors and prior flag mismatch.

Synthetic unit tests validate these safety contracts only.

## Release decision

Even if the mode reduces vertically misdirected tracks, it may
discard valid elevated sources in other scenes. Do NOT activate
it unconditionally in libaudition or ODAS. Require independent
T00 real-room noise, held-out 30/60/120 degree source bearings,
T02/T03/T07/T08, true source-specific SIRi/SI-SDRi, ASR WER,
voice leakage and IDF1 before promotion to a default.

This PR is stacked on libaudition #103; it must remain isolated
from `main`, ODAS `master` and `audition_ros2`.
