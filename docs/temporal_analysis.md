# Temporal analysis foundation (TMP-02 / TMP-03, initial slice)

This branch is stacked on `feat/temporal-acoustic-events` (PR #57), not on `main`.

## TMP-02: scalar temporal evidence

`audition::temporal::TemporalScalarAnalyzer` consumes timestamped scalar
observations. It maintains a bounded, independent EMA and two-sided CUSUM for
each persistent `AcousticSourceId` and feature name. The first sample initializes
the baseline; subsequent samples can produce positive/negative change alarms.
An alarm resets its respective cumulative statistic. CUSUM alarms are change
evidence, **not** a classifier label, calibrated confidence, or measured physical
event onset.

The caller decides which signals have stable scalar semantics (e.g. log-energy,
a calibrated continuous score). A CLAP candidate-relative score must not be
compared across changing candidate sets. No resampling, inference, threads,
automatic calibration, or hidden source association.

Samples must have finite values, stable clock identity, and strictly
increasing timestamps per source/feature. Resource limits throw rather than
silently evicting state. `forget(source)` and `reset()` are explicit.

### Additional TMP-02 algorithms

- `PageHinkleyDetector`: independent, bounded per-source/feature cumulative mean-shift test with configurable allowance, minimum observation count and threshold. A triggered alarm restarts its baseline; it is evidence of distribution change, not a semantic event.
- `TimedActivitySegmenter`: minimum positive duration, negative-evidence hangover, configurable maximum observation gap, stable occurrence IDs, explicit retire and forget. Reported bounds are timestamps of supporting observations. It is not the classifier hysteresis replacement.

## TMP-03: interval relations

`audition::temporal::relate(EventInterval, EventInterval)` implements the
13 mutually exclusive Allen interval relations for two bounded intervals on
the same clock. These are **observed-evidence intervals**, not guaranteed
physical start/end times. Clock mismatches and reversed bounds throw.

### Event episode association (initial TMP-03)

`EventEpisodeAssociator` groups ongoing binary activity of the **same already-resolved** persistent `AcousticSourceId` and class into episodes. Each episode has an instance-unique ID; end transitions retain the last *active observation* timestamp. Optional observation-gap expiry ends an old episode and begins a new one if evidence resumes. Retire ends all active labels in deterministic label order. Forget/reset discard state but never recycle episode IDs within the object lifetime. Strict clock/order and bounded source/class budgets apply.

**Important limitation:** this is temporal episode continuity, not probabilistic multi-target data association. Upstream spatial/identity resolution must establish the persistent source ID. We never equate matching class labels or recycled ODAS track numbers with physical source identity; multi-hypothesis gating and spatial assignment remain follow-ups.

## Validation

GoogleTest cases are registered through the existing CTest discovery:
`TemporalScalarAnalyzer.*` and `TemporalIntervalRelations.*`.
Tests cover EMA/CUSUM, Page-Hinkley shifts, source isolation, capacity, clocks,
invalid samples, all 13 interval relationships, timed segmentation,
discontinuities, episode continuity, label/source isolation,
recycled IDs and explicit retirement.

Still to implement before declaring the full TMP-02/TMP-03 done:
- quantitative time-based segmentation and Page-Hinkley calibration on independent fixtures;
- probabilistic event association/gating with ambiguous sources, duplicate candidates,
  unmatched event lifecycle and spatial track-ID recycling;
- scenario and stress tests; measured timing/memory gates;
- audit matrix entry and package consumer verification.

These tests are deterministic contract tests, not evidence of real-world
change-detection or source-association accuracy.

## TMP-03: spatial-temporal compatibility gate

`SpatialTemporalGate` is a deterministic, pairwise *eligibility test* for two
position observations already transformed by the caller into **one metric frame**.
It validates matching `ClockIdentity`, a configured maximum elapsed interval,
and three-dimensional Euclidean distance. It does not assign, merge, or relabel
persistent source IDs and does not infer whether two observations arise from
the same physical source. Matching labels alone cannot establish identity.

Elapsed nanoseconds are returned as an unsigned 64-bit quantity so extreme
signed timestamp differences do not overflow. The result does not claim that
raw coordinates are comparable without prior sensor-frame transformation.
Potential follow-ups include uncertainty-aware Mahalanobis gating, batch
assignment and ambiguous-candidate handling. The deterministic tests cover
boundary equality, time/space rejection, clock mismatch and integer extremes.

## TMP-03: uncertainty-aware gating and batch matching

`DiagonalMahalanobisGate` computes the squared normalized displacement of
two position estimates using the sum of independent **diagonal** covariance
matrices. It checks clock compatibility, elapsed time, finite coordinates,
nonnegative variances, and a configured squared-distance threshold. Inputs
must already share a metric reference frame. The estimator explicitly does
not support off-diagonal covariance, and the independence assumption must be
justified by the caller. The default threshold is only an illustrative
3-DOF chi-square gate, **not calibrated evidence** of source identity.

`HungarianEventMatcher` implements bounded minimum-cost one-to-one assignment
of observation rows to existing candidate tracks. Missing matrix entries
forbid an edge; each observation can remain unmatched at a configurable
penalty. Results include unmatched tracks and the total caller-defined cost.
All costs must be finite and nonnegative. This matcher does not construct
persistent IDs or independently decide the identity of an acoustic source.

The new deterministic CTest cases check uncertainty sensitivity, wrong
clocks, extreme timestamps, global non-greedy assignment, forbidden edges,
unmatched observations, and malformed matrices.

Remaining follow-up: full-covariance correlated fusion, confidence calibration,
multiple-hypothesis uncertainty, source lifecycle integration, scenario replay,
and real-corpus identity-switch quality gates.
