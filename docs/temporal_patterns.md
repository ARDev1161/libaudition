# TMP-04: acoustic recurrence foundation

`audition::temporal::RecurrenceAnalyzer` computes a bounded, robust
recurrence estimate from ordered event-onset timestamps keyed by a persistent
`AcousticSourceId` and an external event label. Its period is the **lower
median** of positive onset intervals. Jitter is the median absolute
deviation (MAD) divided by the period. The periodic flag is emitted only
after a configured number of intervals; it is an explicit heuristic, not
a calibrated probability or a causal inference.

The algorithm does not use audio samples, ODAS track indices, or speech
transcripts. Source identity resolution and clock assignment occur upstream.
Input clocks must remain stable for each source/label and timestamps must
increase strictly. Intervals exceeding signed `Duration` range are rejected
without changing state. History, sources and labels are bounded, with explicit
`forget()` and `reset()` lifecycle.

CTest / GoogleTest: `RecurrenceAnalyzer.*` covers stable and irregular
sequences, isolation, clock regression, signed timestamp extremes, memory
bounds and history truncation.

This is the first TMP-04 slice. Still open: DTW sequence similarity,
motif discovery, missing-pulse robustness, multiple competing periods,
independently annotated acoustic corpora, calibration and runtime benchmarks.

## Dynamic time warping (DTW)

`DynamicTimeWarping` aligns two nonempty sequences of equal-dimensional
finite feature frames. It uses squared Euclidean frame distance and a
Sakoe–Chiba band (`warping_radius`) with bounded frames and dimensions.
Only two DP rows are retained: memory is O(minimum configured frame capacity)
for the provided second sequence rather than a full alignment matrix.
The API returns cumulative cost, cost divided by selected path length,
path length and explicit `reachable` status. It does not assign a semantic
match label, nor assert a calibrated probability.

CTest / GoogleTest: `DynamicTimeWarping.*` checks exact alignment, temporal
stretch, unreachable narrow bands, invalid input, arithmetic overflow and
frame capacities. Motif discovery and learned feature calibration remain open.
