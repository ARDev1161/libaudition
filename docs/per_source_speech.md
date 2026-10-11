# Per-source speech runtime (foundation)

`PerSourceSpeechRuntime` is a dependency-light, C++17 pipeline for separated
mono PCM streams, keyed by an external acoustic track ID. It does **not**
depend on ODAS, sherpa-onnx, ROS or Qt. Applications supply any
`IVoiceActivityDetector` (which creates an independent `IVadSession`
per source) and an `IAsrEngine` held by `std::shared_ptr<const ...>`.

The capture thread calls `push(track_id, AudioView)` with the source's
mono samples at the configured sample rate (16 kHz by default). Active VAD
frames are concatenated until the VAD first becomes inactive. Completed
speech with at least `min_speech_samples` is moved into the bounded job
queue. The ASR worker transcribes complete segments off the capture thread.
The application drains results with `takeResults()`.

Safety and lifecycle:
- Independent VAD state and PCM accumulation for each source;
- At most `max_sources` active source sessions and `max_pending_segments`
  waiting jobs/results;
- `max_segment_samples` caps speech buffers to prevent unbounded growth;
- `forget(track_id)` invalidates pending/inflight results from retired IDs;
- `reset()` invalidates source generations and jobs after a discontinuity;
- Per-runtime `statistics()` reports completions, losses, and last ASR error.

This is an **initial foundation**, not the complete low-latency ASR pipeline:
VAD currently gates whole pushed chunks, without pre-roll, post-roll,
partial transcripts or time-corrected onset/end timestamps. It is suitable
for mocked/controlled tests, not a claim of on-device recognition quality.
The next iteration must add optional VAD pre/post-roll and an ODAS/Qt adapter
with actual Sherpa models, error propagation and per-source transcript UI.
The client controls capture, retention and whether speech may be recorded.

The new PR is stacked on classification-runtime PR #53; neither is merged
into `main` automatically.
