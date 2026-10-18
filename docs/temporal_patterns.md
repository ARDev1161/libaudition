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

## Offline acoustic motif discovery

`AcousticMotifFinder` scans fixed-length feature windows and compares
non-overlapping candidate pairs with the bounded DTW engine. It reports the
lowest normalized DTW cost only if it meets an explicit caller-selected
threshold. Window length, scanning stride, maximum frames/dimensions and DTW
warping radius are configurable. The implementation does not produce semantic
class labels and does not treat overlapping windows as independent matches.

Complexity is quadratic in the number of candidate windows, multiplied by
the bounded DTW window-alignment cost. This initial algorithm targets bounded
short recordings rather than long continuous streams.

CTest: `AcousticMotifFinder.*` exercises repeated and unrelated motifs,
insufficient history, malformed feature frames, capacity and configuration
validation. Real annotated repeat detection, cross-source matching, false
positive rates and streaming motif indexing are not yet measured.

### Multiple disjoint motifs

`AcousticMotifFinder::findMultiple(frames, max_matches)` enumerates valid non-overlapping DTW motif pairs, orders them by normalized cost with deterministic tie breaking, and greedily selects up to `max_matches` pairs with no frame-window overlap across selected matches. The result cap bounds output size; input frame/window capacities still bound computation. Greedy disjoint selection is **not** a proof of globally optimal motif-set coverage. `AcousticMotifFinder.Multiple*` CTest cases cover independent repetitions, overlap exclusion and result-cap validation.

## Streaming template recognition

`StreamingPatternMatcher` stores a bounded rolling feature history per
persistent acoustic source, matches the last complete window against
registered templates using DTW, and returns matching labels, costs and
source-local frame indices. It never combines frames from different sources.
Templates are validated on registration; malformed incoming frames are
rejected before history commit. `forget(source)` clears a source's history.

This is an intentionally simple **fixed-length trailing-window** matcher:
it cannot yet handle deletion of an entire event, variable-length candidate
boundaries or timestamp-based silence gaps. It can emit the same label at
successive frame positions; consumers must supply deduplication/cooldown
policy. `StreamingPatternMatcher.*` CTest scenarios verify completion,
source isolation, rollover/recovery, invalid frame rejection and capacity.

### Per-source template alert cooldown

`StreamingPatternOptions::cooldown_frames` suppresses repeat notifications from
an identical template label for a specific persistent source within the
configured number of subsequently accepted frames. The default `0` retains
the original every-frame emission behavior. Cooldowns are independent across
labels and sources, and `forget(source)` / `reset()` clear emission state.
Cooldown is measured in feature frames, **not wall-clock time**, and does not
repair missing observations. A wrap of the source-local frame counter is
rejected before committing a new frame.
