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

### Variable-length suffix alignment

`StreamingPatternOptions::min_candidate_frames` and `max_candidate_frames` optionally define an inclusive range of trailing observation lengths evaluated against each registered feature template. Zero means use the template length (the backward-compatible default). The matcher selects the lowest normalized reachable DTW cost in the configured range, then applies the source/label cooldown. Candidate lengths are bounded by `max_frames`; candidates shorter than a template can align repeated features but **cannot recover arbitrary deleted information**. The configured warping radius may make unequal lengths unreachable. Regression tests cover shortened plateau patterns, nonmatching deleted features, and default fixed-length behavior.

### Streaming recovery and computational bounds

`StreamingPatternMatcher` retains at most `max_frames` feature frames
per persistent source. Each accepted frame is compared against every
registered template and each permitted trailing candidate length, using
two-row banded DTW. With `T` templates, `L` candidate lengths, template
length `W`, candidate length `C` and feature dimension `D`, a conservative
per-frame arithmetic bound is O(T * L * W * C * D); time and storage are
bounded by the configured caps, but no wall-clock latency guarantee is
asserted. The current matcher allocates temporary candidate vectors; memory
allocations and latency should be benchmarked before realtime deployment.

`StreamingPatternMatcher.MissingMiddleFrameWithoutEquivalentPlateauDoesNotMatch`
demonstrates that an arbitrary deleted discriminating frame does **not**
become an exact match. Additional scenarios test recovery on a subsequent
complete occurrence, rollover after 100 unrelated frames, independent
template cooldown and rejection of candidate bounds exceeding history.
These tests measure deterministic behavior, not physical dropped-frame rates.

### Suffix construction optimization

Candidate suffix frames are now materialized at most once for each distinct candidate length during a `push()`, then shared across all registered templates with that length. This reduces redundant window copying when many templates are active, without changing DTW matching or transactional state updates. **The source state is still copied before evaluation**, and DTW still allocates its dynamic-programming rows per comparison; no allocation-free or real-time bound is claimed. `StreamingPatternMatcher.SharedSuffixCandidatesPreserveMultipleTemplateResults` checks outputs with multiple templates and variable windows.

### Reusable DTW scratch buffers

`DynamicTimeWarping::compare(a,b,DtwWorkspace&)` allows callers to retain the two dynamic-programming rows between comparisons. The original two-argument `compare` API remains unchanged and creates a local workspace. `StreamingPatternMatcher` shares one local workspace across template comparisons **within each push**, reducing repeated DP-row allocations but not eliminating candidate-window/state copying. Workspace use is not thread-safe when shared concurrently; use separate workspaces per thread/call. `DynamicTimeWarping.ReusedWorkspace*` verifies equivalence across differing lengths and recovery after rejected samples.

### Virtual suffix evaluation

Streaming matching now evaluates each candidate suffix as the existing source history followed by the prospective incoming frame. Previous history is not cloned per `push()`. DTW computations and cooldown-map preparation complete before the new frame is committed. Candidate suffixes and temporary DTW rows still allocate; this is not an allocation-free real-time guarantee. The failure-regression test confirms an arithmetic DTW exception leaves the prior feature sequence and frame index unchanged.

### Streaming throughput smoke measurement

`StreamingPatternBenchmark.MultisourceMultitemplateThroughputSmoke` executes a deterministic 10-source, 8-template, 100-frame-per-source load (1000 `push()` calls), reports total elapsed microseconds, mean time per push and emitted match count through CTest logs. It is intentionally an informational microbenchmark without a runner-dependent latency threshold; it does not measure heap allocation counts, worst-case latency, real-time scheduling or production hardware. Those require dedicated profiling and explicit environments.

### Streaming scaling observations

The CTest microbenchmark now reports worst observed `push()` duration in addition to average throughput. `StreamingPatternBenchmark.TemplateAndSourceScalingMeasurements` prints a deterministic 3×3 sweep of 1/5/10 sources and 1/4/8 templates, with 40 frames per source. CI timings are descriptive measurements only, not latency assertions. No heap-allocation counts or real-time deadline certification is implied.

### Isolated C++ allocation probe

`temporal_streaming_allocation_probe` is a standalone CTest executable that counts calls to global C++ `operator new` and `new[]` after a warmup of 30 frames per source. The measured workload uses 10 sources, 8 templates, and 100 frames per source, reporting absolute allocation calls and per-push mean. Its overrides are isolated from the main GoogleTest binary. This is diagnostic only: it does not count direct `malloc`, allocator-specific internal allocations or bytes, and compiler/platform differences mean no fixed allocation threshold is imposed in CI.

The allocation probe has a CTest `PASS_REGULAR_EXPRESSION` contract requiring the metric marker, guarding against silent no-op execution. Successful test stdout may still be hidden in the default non-verbose CTest log; use `ctest -V -R '^temporal_streaming_allocation_probe$'` to inspect numerical results locally or in a dedicated verbose CI step. No absolute allocation reduction is claimed yet.

The probe remains registered in CTest. To view its numeric diagnostics, run `ctest --test-dir build/ci -V -R temporal_streaming_allocation_probe`; ordinary CI output may hide successful test stdout. No allocation budget is enforced.
