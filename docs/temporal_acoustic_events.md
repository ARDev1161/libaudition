# Temporal acoustic event detection (v0.6)

## Purpose and architectural ownership

`TemporalAcousticEventDetector` is a synchronous, backend-independent
algorithm that converts timestamped classification observations into discrete
acoustic activity **start/end transitions**.

The caller owns model inference (YAMNet, EfficientAT, CLAP, Sherpa, etc.),
source identity resolution, observation timestamps, scheduling and mapping
transitions to `AcousticEventAssembler`. The algorithm does not run threads,
parse audio, create ROS messages or decide how long a final application-level
`AcousticEvent` must live.

An example application pipeline is:

~~~text
audio -> classifier -> {persistent AcousticSourceId, observation Timestamp,
                       ClassificationResult}
                        -> TemporalAcousticEventDetector
                        -> Started/Ended transitions
                        -> application-controlled AcousticEventAssembler
~~~

These classification **scores are input evidence**, not calibrated probabilities
of acoustic-event occurrence. CLAP class similarity softmaxes, for instance,
are relative to the candidate list: a fixed threshold is not automatically
comparable between different vocabularies, models or scores. Select an
appropriate observation source, stable vocabulary and threshold calibration
before interpreting detections as acoustic facts.

## Explicit temporal contract

- One `AcousticSourceId` identifies a persistent acoustic source; classes are
  tracked separately. Temporary ODAS track IDs should be resolved *before*
  this stage. Distinct sources may emit the same class concurrently.
- Call `observe(source, time, scores)` in **strictly increasing observation time**
  for each source. Timestamp is the time of evidence (e.g. classifier window
  end), not when a worker happened to finish inference. Source clocks must not
  switch unexpectedly. Multiple sources may use different clock identities,
  but one source's epoch must remain consistent.
- Missing classes count as score 0. Duplicate/empty labels fail atomically.
  Classes below the start threshold do not occupy memory.
- A new class becomes active only after `min_start_observations` consecutive
  observations at or above `start_threshold`. Its emitted start timestamp
  is the **first qualifying observation**, not the unknown onset of the
  original acoustic waveform.
- Active classes are retained while the score is at or above `stop_threshold`.
  Once `min_stop_observations` consecutive scores fall below that threshold,
  an Ended transition is emitted. Shorter dips merge into one occurrence;
  the end timestamp is the **last observation supporting activity**.
- Optional `max_observation_gap` causes discontinuous streams to close active
  events at their last supported time, then start fresh candidates.
  Zero disables gap-based closing; in that mode, callers must use `retire()`
  or explicitly supply low-score observations to end silent/disconnected
  sources.
- `retire(source, time)` validates the time and returns Ended transitions for
  confirmed classes; candidate-only activity is discarded. `forget()` and
  `reset()` are explicit hard discontinuities that discard activity without
  emitting Ended, e.g. after unrecoverable capture reset.
- Each Started/Ended pair has a stable `occurrence_id`, never reused within
  one detector instance even after source forget or global reset. Occurrences
  for a class do not overlap on a source. `activeEvents()` returns snapshots
  sorted by source ID and label.
- The detector retains at most `max_sources` and `max_labels_per_source`
  candidate/active class states. Exceeding capacity rejects the *whole*
  observation transactionally, with no implicit eviction of live events.
  The caller must retire/forget obsolete sources.

Observations are ordered by **observation count** for hysteresis, rather than
pretending that overlapping sliding windows are disjoint spans of audio.
A caller wanting millisecond-scale timing must choose appropriate observation
hop sizes and account for classifier windowing latency.

## Example usage (C++17)

~~~cpp
#include <audition/pipeline/temporal_acoustic_event_detector.hpp>
#include <audition/pipeline/acoustic_event_assembler.hpp>

audition::TemporalAcousticEventDetectorOptions opts{};
opts.start_threshold = audition::Probability::from(0.75);
opts.stop_threshold = audition::Probability::from(0.35);
opts.min_start_observations = 2;
opts.min_stop_observations = 3;
opts.max_observation_gap = audition::Duration::fromChrono(
    std::chrono::milliseconds{2000});
audition::TemporalAcousticEventDetector detector{opts};

// ClassificationResult is produced elsewhere. A real app associates
// the persistent source, chosen model and classifier window-end timestamp.
auto changes = detector.observe(
    source_id, observed_window_end, classification);
for (const auto& change : changes) {
    if (change.kind == audition::TemporalAcousticEventKind::Started) {
        // Begin a caller-owned AcousticEvent at change.start_time and remember
        // change.occurrence_id -> AcousticEventId in application state.
    } else {
        // Finish the corresponding AcousticEvent at
        // change.last_supported_time; preserve source and class evidence.
    }
}
~~~

Do not update an `AcousticEventAssembler` event beyond the last supported
observation and then attempt to finish it earlier: the assembler correctly
rejects decreasing timestamps. The detector emits only classifier transitions;
the caller is responsible for combining additional spatial and speech evidence
and choosing its higher-level event lifecycle.

## Validation

`tests/temporal_acoustic_event_detector_test.cpp` registers
`TemporalAcousticEventDetector.*` with GoogleTest/CTest in the core job.
It checks threshold and hysteresis boundaries, short dips, source and class
isolation, occurrence ID lifecycle, hard discontinuities, gap handling,
transactional invalid input/capacity, clock ordering, multi-source boundedness
and explicit `AcousticEventAssembler` composition.

These deterministic tests do **not** establish real acoustic classification
precision/recall, false alarms per hour, detection onset error in milliseconds,
or robustness against a changing physical environment. Those require versioned
independently annotated real-audio corpora and a documented per-model protocol.
