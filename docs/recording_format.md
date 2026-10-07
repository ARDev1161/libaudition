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
