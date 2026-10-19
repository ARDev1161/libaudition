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

TMP-04 now includes bounded DTW sequence similarity, offline motif
discovery, source-local streaming template recognition, configurable
candidate lengths, cooldown handling, and CI allocation/latency probes.
Open work includes independently annotated real-world evaluation,
calibrated detection thresholds, missing-pulse models, competing-period
estimation and real-time guarantees on target hardware.

## Dynamic time warping (DTW)

`DynamicTimeWarping` aligns two nonempty sequences of equal-dimensional
finite feature frames. It uses squared Euclidean frame distance and a
Sakoe–Chiba band (`warping_radius`) with bounded frames and dimensions.
Only two DP rows are retained: scratch memory is O(length of the second
sequence), capped by `max_frames`, rather than a full alignment matrix.
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

`AcousticMotifFinder::findMultiple(frames, max_matches)` orders valid non-overlapping DTW motif pairs by normalized cost with deterministic tie breaking, then greedily selects disjoint pairs. For `max_matches == 1`, it delegates to the single-best-pair search and avoids materializing the quadratic candidate list. For larger result limits it still retains all qualifying candidates before sorting: `max_matches` caps returned results, **not** temporary candidate storage. Input frame/window capacities bound computation. Greedy disjoint selection is **not** a proof of globally optimal motif-set coverage. `AcousticMotifFinder.Multiple*` CTest cases cover independent repetitions, overlap exclusion and result-cap validation.

Offline motif scans reuse invocation-local DTW row buffers across window comparisons. This reduces repeated DP-row allocations without sharing mutable scratch between concurrent `const` calls. It avoids per-window feature copies, but candidate comparisons remain quadratic;
for `max_matches > 1` temporary candidates can also grow quadratically. Keep offline motif discovery outside hard real-time callbacks until representative performance and memory measurements are available.

## Streaming template recognition

`StreamingPatternMatcher` stores a bounded rolling feature history per
persistent acoustic source, matches the last complete window against
registered templates using DTW, and returns matching labels, costs and
source-local frame indices. It never combines frames from different sources.
Templates are validated on registration; malformed incoming frames are
rejected before history commit. `forget(source)` clears a source's history.

The default matcher uses a **fixed-length trailing window**, and an
optional candidate-length range supports shorter or longer feature
sequences. It does not reconstruct wholly missing discriminating events
or infer timestamp-based silence gaps. Repeated labels can be suppressed
by a source-local, per-label cooldown measured in frames.
`StreamingPatternMatcher.*` CTest scenarios cover completion, source
isolation, rollover/recovery, invalid frames, capacity, cooldown and
dynamic template lifecycle.

### Per-source template alert cooldown

`StreamingPatternOptions::cooldown_frames` suppresses repeat notifications from
an identical template label for a specific persistent source within the
configured number of subsequently accepted frames. The default `0` retains
the original every-frame emission behavior. Cooldowns are independent across
labels and sources, and `forget(source)` / `reset()` clear emission state.
Cooldown is measured in feature frames, **not wall-clock time**, and does not
repair missing observations. A wrap of the source-local frame counter is
rejected before committing a new frame.

### Dynamic template lifecycle

`registerTemplate(label, frames)` atomically validates and inserts or
replaces a template. Successful replacement clears prior cooldown records
for that label across all sources, because previously suppressed detections
refer to a different template definition. Feature histories and cooldown
records for other labels are retained. Invalid replacement arguments leave
both the prior template and its suppression records unchanged.

`unregisterTemplate(label)` returns true if a template existed and removes
only that label and its source-local cooldown records. An unknown label
returns false without changing state. Removing a template releases one slot
from `max_templates`; later re-registration of the same label can emit
immediately. Neither operation discards source histories. Like other matcher
mutations, template registration/removal must not race with `push()`.

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
asserted. The matcher reuses candidate suffix buffers across calls; retained scratch
can grow with the number and sizes of candidate lengths. Memory allocations,
peak retained bytes and latency must be benchmarked on target hardware before
real-time deployment.

`StreamingPatternMatcher.MissingMiddleFrameWithoutEquivalentPlateauDoesNotMatch`
demonstrates that an arbitrary deleted discriminating frame does **not**
become an exact match. Additional scenarios test recovery on a subsequent
complete occurrence, rollover after 100 unrelated frames, independent
template cooldown and rejection of candidate bounds exceeding history.
These tests measure deterministic behavior, not physical dropped-frame rates.

### Suffix construction optimization

Candidate suffix frames are now materialized at most once for each distinct candidate length during a `push()`, then shared across all registered templates with that length. This reduces redundant window copying when many templates are active, without changing DTW matching or transactional state updates. **Committed source history is not copied before evaluation**: candidate
suffixes are filled from existing history plus the incoming frame, and DTW
reuses scratch rows. No general allocation-free or real-time bound is claimed. `StreamingPatternMatcher.SharedSuffixCandidatesPreserveMultipleTemplateResults` checks outputs with multiple templates and variable windows.

### Reusable DTW scratch buffers

`DynamicTimeWarping::compare(a,b,DtwWorkspace&)` allows callers to retain the two dynamic-programming rows between comparisons. The original two-argument `compare` API remains unchanged and creates a local workspace. `StreamingPatternMatcher` shares one local workspace across template comparisons **within each push**, reducing repeated DP-row allocations but not eliminating candidate-window/state copying. Workspace use is not thread-safe when shared concurrently; use separate workspaces per thread/call. `DynamicTimeWarping.ReusedWorkspace*` verifies equivalence across differing lengths and recovery after rejected samples.

### Virtual suffix evaluation

Streaming matching now evaluates each candidate suffix as the existing source history followed by the prospective incoming frame. Previous history is not cloned per `push()`. DTW computations and cooldown-map preparation complete before the new frame is committed. Candidate suffix and DTW row buffers are reused after warmup, but growth or configuration changes can allocate; this is not a real-time guarantee. The failure-regression test confirms an arithmetic DTW exception leaves the prior feature sequence and frame index unchanged.

### Streaming throughput smoke measurement

`StreamingPatternBenchmark.MultisourceMultitemplateThroughputSmoke` executes a deterministic 10-source, 8-template, 100-frame-per-source load (1000 `push()` calls), reports total elapsed microseconds, mean time per push and emitted match count through CTest logs. It is intentionally an informational microbenchmark without a runner-dependent latency threshold; it does not measure heap allocation counts, worst-case latency, real-time scheduling or production hardware. Those require dedicated profiling and explicit environments.

### Streaming scaling observations

The CTest microbenchmark now reports worst observed `push()` duration in addition to average throughput. `StreamingPatternBenchmark.TemplateAndSourceScalingMeasurements` prints a deterministic 3×3 sweep of 1/5/10 sources and 1/4/8 templates, with 40 frames per source. CI timings are descriptive measurements only, not latency assertions. No heap-allocation counts or real-time deadline certification is implied.

### Isolated C++ allocation probe

`temporal_streaming_allocation_probe` is a standalone CTest executable that counts calls to global C++ `operator new` and `new[]` after a warmup of 30 frames per source. The measured workload uses 10 sources, 8 templates, and 100 frames per source, reporting absolute allocation calls and per-push mean. Its overrides are isolated from the main GoogleTest binary. This is diagnostic only: it does not count direct `malloc`, allocator-specific internal allocations or bytes, and compiler/platform differences mean no fixed allocation threshold is imposed in CI.

The allocation probe has a CTest `PASS_REGULAR_EXPRESSION` contract requiring the metric marker, guarding against silent no-op execution. Successful test stdout may still be hidden in the default non-verbose CTest log; use `ctest -V -R '^temporal_streaming_allocation_probe$'` to inspect numerical results locally or in a dedicated verbose CI step. The measured zero-cooldown workload in Ubuntu CI #1360 (19 October 2026)
reported 130 C++ allocations for 1000 returning `push()` calls versus zero
for 1000 caller-buffer-reusing `pushInto()` calls after warmup. These counts
exclude caller-owned input construction, direct `malloc` and allocated bytes;
they do not establish a general zero-allocation guarantee.

The probe remains registered in CTest. To view its numeric diagnostics, run `ctest --test-dir build/ci -V -R temporal_streaming_allocation_probe`; ordinary CI output may hide successful test stdout. No allocation budget is enforced.

### DTW scratch reuse

`StreamingPatternMatcher` retains a single `DtwWorkspace` across sequential `push()` calls rather than allocating a new scratch object for every frame. The instance already owns mutable per-source state and remains **not thread-safe**: synchronize concurrent access or use one matcher per processing thread. DTW arithmetic failures still occur before accepted history/cooldown is committed. The allocation impact should be measured with the standalone probe; it has not yet been quantified.

### Reusable suffix feature buffers

Candidate suffix windows now retain their outer and per-frame vector capacity across sequential `push()` calls, keyed by candidate length. Each call invalidates cached content and refills only lengths requested by templates, preventing reuse of stale source frames when streams interleave. Buffer contents are scratch state (not committed source history), so failed DTW comparisons still leave source history and cooldown unmodified. This trades retained bounded scratch memory for fewer repeat allocations, but still copies feature values into DTW-compatible contiguous candidate vectors; allocation reductions are not yet benchmark-verified.

The allocation probe additionally reports `max_cpp_new_per_push` and `zero_allocation_pushes` for the measured 1000 calls. Counts include any C++ allocation within each synchronous `push()` call; they are diagnostic rather than CI acceptance thresholds. For comparable baselines, use the same build configuration and runner, and inspect verbose CTest output.

### Full-history storage recycling

When a source's history reaches `max_frames`, `push()` now fills a reusable staging vector, swaps it into the oldest slot, and rotates the bounded deque of feature vectors. This avoids allocating a new feature vector on each steady-state frame; a failed staging copy cannot mutate committed history. Rotation costs O(`max_frames`) vector swaps per full-history push, so benchmark both allocation count and elapsed time before treating this as an overall speedup. Sources remain isolated.

### Allocation attribution

The standalone probe additionally groups measured `operator new` calls into pushes that emitted matches (`matching_pushes`, `matching_allocations`) versus pushes that emitted no matches (`nonmatching_allocations`). These categories help distinguish baseline per-frame allocations from alert/cooldown bookkeeping. The current measured baseline before attribution was 1260 C++ allocations over 1000 pushes (1.26/push) in Ubuntu CI #1329, down from 2310 in #1326 (same configured workload). This is not a throughput or allocation-free guarantee.

**Measurement correction (following CI #1331):** Previous `cpp_new_calls` included one allocation per push from the probe's own temporary input `std::vector<double>` construction. The probe now preallocates and reuses that caller-owned input outside the measured interval. Prior totals (2310 and 1260) are *not* pure matcher-internal allocation counts; compare corrected runs with the same probe version before attributing allocation changes to the matcher.

### Verified steady-state allocation baseline and cooldown optimization

Ubuntu CI #1333 measured the corrected caller-buffer-excluded baseline: 1000 `push()` calls across 10 sources and 8 templates produced 260 C++ `operator new` calls (0.26 per push), with 870 allocation-free nonmatching pushes and 130 matching pushes accounting for all 260 allocations. The probe does **not** capture direct `malloc`, bytes allocated or system allocator behavior.

When `cooldown_frames == 0`, repeated notifications are intentionally never suppressed. The matcher therefore no longer copies or updates `last_emission` for matching frames in that configuration. Positive cooldown values retain pre-commit staging of emission bookkeeping for transactional history/cooldown semantics. The effect of this optimization must be verified by a new corrected probe run, not inferred from the source change.

### Caller-owned result storage

For sustained audio streams, use `pushInto(source, features, results)` rather than
`push(source, features)` when result-vector allocations matter. Preallocate
`results.reserve(max_templates)` once and reuse the vector across calls.
The matcher clears it after input validation, then fills it with detections;
an empty result is an empty vector, not stale results from the preceding frame.
The original returning-vector API remains available and behaves identically.

The matcher commits history and cooldown only after comparisons and result
construction succeed. If an exception occurs during comparison or output
growth, source state is unchanged, but the caller-owned result vector may
contain partial detections and must be discarded. Neither API is safe for
concurrent calls on the same matcher without external synchronization.
The new API reduces avoidable result-vector allocation opportunities but
does not promise allocation-free operation for arbitrary label lengths,
template counts, or workloads. A paired measurement is available in Ubuntu CI #1360; its scoped result
is 130 versus 0 C++ allocations across 1000 calls for returning and
caller-buffer-reusing APIs respectively (same deterministic input stream).

### Independent exact-match acceptance oracles

To detect regressions beyond hand-authored examples, CTest includes an
independent 2400-step, seeded streaming oracle. With zero warping radius
and zero score tolerance, its reference implementation uses ordinary
suffix equality and per-source/per-label frame counters; it checks both
`push()` and reusable `pushInto()`, source forget/reset, cooldown and
rolling-window boundaries without calling DTW internals. A second oracle
exhaustively enumerates all 256 binary 8-frame sequences, compares the
motif finder's best pair to exact window equality and checks up to two
greedily selected disjoint motif pairs using a separate frame-occupancy
model. These are synthetic correctness checks, **not** field accuracy
measurements or a substitute for representative audio recordings.

### Offline motif search scratch reuse and diagnostics

`AcousticMotifFinder::find()` and `findMultiple()` now compare
windows directly within the caller's existing feature sequence, avoiding
temporary copies of the left/right feature frames. The core DTW engine
exposes `compareWindows(frames, first_start, second_start, length, workspace)`,
which validates bounded offset ranges, dimensions and numeric input. The
existing `compare(a,b)` APIs delegate to the same two-row DP engine.
All motif calls keep `DtwWorkspace` invocation-local, so concurrent
`const` calls do not share mutable scratch. The underlying candidate
search remains quadratic; this optimization does not change the
greedy best/disjoint selection contract.

`AcousticMotifFinder.ReusedWindowsPreserveMultidimensionalMatchesAcrossCalls`
checks repeated multidimensional matches and negative controls.
The isolated `temporal_motif_allocation_probe` runs 32-frame, 4-feature,
4-frame-window motif searches, reports C++ allocation calls and mean
execution time per `find`/`findMultiple` invocation, and verifies the
expected exact motif. Ubuntu CI prints it together with the existing
streaming allocation probe. All timings and counts are diagnostic; no
cross-runner allocation or latency thresholds are implied.

`DynamicTimeWarping.WindowComparisonMatchesCopiedSequenceOracle` now compares
thousands of in-place DTW windows to the original copied-sequence API
across multiple dimensions, offsets, lengths and warping radii. Additional
tests check out-of-range offsets (including `SIZE_MAX`), zero/capacity
limits, invalid feature shapes, numeric overflow and workspace recovery.
The motif allocation probe remains the quantitative baseline; allocation
or latency improvements from this change are not asserted until the
updated CI publishes them.

### Single-pass feature validation in motif searches

The public `DynamicTimeWarping::compare()` and `compareWindows()` calls
continue to validate frame dimensions, finiteness and input bounds for
every invocation. The offline motif finder separately validates **all**
input feature frames once before enumerating any DTW candidate pair, then
uses an internal, friend-only `compareValidatedWindows()` path. This avoids
repeating the expensive frame validation for every pair; it does not
weaken the public API's safety contract or allow invalid input to pass
the motif finder. Regression tests exercise invalid middle frames, both
best-pair and multi-pair searches, and recovery after exceptions. Numeric
cost overflow remains checked per candidate. Benchmark improvements
should be assessed from the subsequent CI probe.
