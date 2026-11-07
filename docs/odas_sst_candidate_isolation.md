# Experimental ODAS SSL-to-SST hypothesis isolation

This is a **separate reviewable change** stacked on [the Acoustix fixture PR](https://github.com/ARDev1161/libaudition/pull/72). It is **not** an upstream ODAS patch, does not change the default audio path, and must not be treated as a proven speaker-separation fix.

## Root cause / motivation

The pinned `introlab/odas` source (`src/system/mixture2mixture.c`)
computes likelihoods over joint candidate-to-track assignments, multiplies
the per-candidate likelihood and prior terms, and normalizes the full
combination space into `postprobs->arrayNew`. The SST dynamic birth rule
(`src/module/mod_sst.c`) requires `arrayNew[i] > theta_new`.

In the deterministic 7×7 m Acoustix recording, **SST capacity alone** is
not the cause: 4 SSL candidates with 4 or 8 SST tracks yield identical
outputs. Increasing SSL candidate capacity from 4 through 8 causes
progressive deterioration, with no tracks at 7 or 8. This is diagnostic
evidence that adding extra weak candidates changes joint assignment
probabilities. It does not by itself establish the best general policy.

## Change

`OdasSstOptions::max_input_potentials` is optional (0 by default):

- **0**: unchanged original behavior; all `ssl.potential_source_count`
  hypotheses enter the ODAS SST module.
- **1..SSL count**: maintain an **independent ODAS potential message** of
  that capacity for SST, filled from the highest-scoring SSL candidates.
  The selection is deterministic (input slot breaks score ties) and
  preallocates its index buffer during initialization; no heap allocation
  in the audio processing path.
- All SSL candidates remain available in the original buffer and in
  `SpatialProcessingResult::potential_sources`; only SST association input
  is restricted. The separated audio still comes from the same ODAS
  SST/SSS output. Reset rebuilds both messages safely.
- Invalid input limits greater than the SSL candidate count fail
  validation rather than being silently truncated.

The new option can alter track acquisition, so it is **disabled by
default**. It can also hide a legitimate source when the array cannot
resolve enough peaks; a four-candidate limit is **not** a universal
solution for seven simultaneous speakers.

## Independent comparison and CI

`audition_offline_integration` accepts
`--max-tracks 8 --potential-sources 8 --sst-input-potentials 4`.
The original flags still default to legacy 4/4 with no limit.

The headless Acoustix CTest runs on the **same exact WAV** with:

- legacy 4/4 and SSL 4..8 / SST 8 capacity sweep;
- unbounded 8/8 (all eight hypotheses to SST);
- isolated 8/8 with four strongest hypotheses delivered to SST.

Outputs include all raw SSL proposals, track observations,
`metrics_8x8_sst4.json` and the updated
`capacity_comparison.json`. The GitHub Actions job summary displays
both 8/8 variants side by side. The isolated case is required to
complete without errors/dropped frames; the quantitative outcome
should be examined before choosing any performance gate.

The normal CTest matrix also checks the option validation, runtime
creation, processing, and reset. After verification, compare the
number of tracked speakers, unique source-hop matches and angular
errors **without changing arbitrary SST probability thresholds**.
Use real-speech recordings and measured arrays before making quality
or production claims.

This change should only be merged after PR #72 is accepted and
its Acoustix test environment is available.
