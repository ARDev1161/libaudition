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
VAD gates whole pushed chunks with configurable pre-roll and post-roll, but without
partial transcripts or independent phoneme-precise VAD onset estimates. It is suitable
for mocked/controlled tests, not a claim of on-device recognition quality.
The next iteration must add optional VAD pre/post-roll and an ODAS/Qt adapter
with actual Sherpa models, error propagation and per-source transcript UI.
The client controls capture, retention and whether speech may be recorded.

The new PR is stacked on classification-runtime PR #53; neither is merged
into `main` automatically.

## Optional Qt ODAS integration (Sherpa)

With `LIBAUDITION_BUILD_QT_DEMO=ON`, `LIBAUDITION_WITH_ODAS=ON`
and `LIBAUDITION_WITH_SHERPA=ON`, the Acoustic scene now has an
independent **Transcribe separated sources (Silero + SenseVoice)** checkbox.
Choose existing local Silero VAD ONNX, SenseVoice ONNX and matching
SenseVoice `tokens.txt`. The Qt demo **does not download these models**.

For each ODAS SSS mono frame, the adapter pushes 16-kHz audio keyed by the
ODAS numeric track ID. A separate VAD session gates each source, then offline
ASR runs on a bounded worker queue. Completed transcriptions appear in the
`Transcript` column of that same source row. On an ALSA overrun, ODAS reset,
or source retirement, earlier unfinished audio/jobs and cached text are
invalidated to avoid misattributing speech to a reused track ID.

The original sound-event classification remains fully independent:
YAMNet can be enabled simultaneously, or the per-source ASR path alone may
activate ODAS SSS. No ROS, cameras, radar or robot decisions are added to
libaudition. VAD/ASR models are supplied by the application.

Caveats: the first integration has a single shared offline ASR worker,
no incremental partial transcript UI, and does not automatically download
any third-party ASR model. It does not yet validate speech accuracy on real
ODAS-separated conversations or prove real-time performance on the RK3588.

## Speech continuity, fault isolation and observability

- Positive-VAD audio is split into consecutive bounded segments when the
  configured `max_segment_samples` limit is reached. Even if the input
  frame straddles a split, **all** its PCM samples are preserved. The
  new segment's `SpeechSegment.audio.captureTime()` advances by the exact
  sample offset relative to the caller's frame timestamp.
- The speech minimum is measured in voiced samples, not padded silence.
  Pre-roll starts at the proper earlier audio timestamp; post-roll includes
  at most the configured number of silence samples.
- VAD exceptions, null VAD sessions, and NaN/Inf from **one** separated
  source quarantine that track without terminating ALSA capture or the
  processing of other sources. The new `trackStatus(track_id)` exposes
  active speech, buffered samples, and any track-local error. A caller
  retires the ID with `forget()` before retrying.
- The Qt Acoustic scene now shows VAD state, ASR completed/pending/drop
  counters and per-source VAD/ASR failures in the live diagnostics.

The C++ regression suite checks multi-source separation, pre/post roll,
absence of sample loss at segment boundaries, audio-clock continuity,
single-track VAD failure isolation, and explicit reuse after `forget()`.
The Sherpa CI job also runs a real ONNX `SherpaOfflineAsr` (Whisper tiny.en
INT8) behind the same runtime for two source IDs; its predicted **text**
is deliberately not asserted on synthetic input. This validates backend
wiring and source associations rather than recognition accuracy.
