# Recorded scenario format (draft v0.1)

Recorded scenarios are intentionally middleware-neutral and are meant for
reproducible regression/benchmark runs.

```text
session/
  manifest.json
  raw/
    array.wav
  tracks/
    track-0001.wav
  poses/
    sensor_pose.csv
  annotations/
    ground_truth.json
  results/
    <run-id>.json
```

`manifest.json` records clock domain, sample format, microphone geometry,
hardware/session metadata, recording hashes, and schema version. Inference
results are append-only per run and contain backend/model revision metadata so a
new model never destroys previous results.

## Source sound-annotation semantics

A recording may retain results from multiple classifiers, and a single
classifier may report multiple labels for the same audio window. The
source-recording coordinator suppresses only an exact duplicate
`(RecordingId, model, stage, start_sample, end_sample, label)`.
Different labels, models, stages, or window boundaries remain separate
events; duplicate confidence updates for the same key do not create
additional events.

The coordinator also discards invalid probabilities (nonfinite or
outside the closed interval `[0, 1]`) and increments its sound drop
counter instead of throwing into the capture/recording owner loop.
The underlying `RecordingTimeline` retains strict score validation.
Invalid scores do not reserve a deduplication key. Annotation
limits and text-byte budgets still apply.

## Concurrent JSON sidecar writes

`writeRecordingTimelineJson()` now reserves a unique directory next to
the destination and writes the complete JSON inside it before rename.
This prevents concurrent producers from using the same predictable
`<file>.tmp` staging path and truncating one another's in-progress
documents. The staging directory is cleaned on success and on error,
and a legacy `.tmp` file is not touched. Simultaneous writes to the
same destination are **last-rename-wins**, not a merge; clients needing
ordered history must coordinate writer ownership. Atomic rename
does not guarantee power-loss durability without an explicit fsync
policy. A CTest regression covers concurrent writers, preserved
legacy temporary data, and cleanup after a failed publication.
