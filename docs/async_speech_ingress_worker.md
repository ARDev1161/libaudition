# Asynchronous source speech worker

`AsyncSpeechIngressWorker` is the explicit C++17 scheduling adapter
between producer PCM and the existing `PerSourceSpeechRuntime`.

A capture producer calls `push(source_id, AudioView)` and records the
accepted `sourceEpoch(source_id)` alongside its **RecordingId**.
The separate worker owns the execution of VAD `push()`, `retire()`
and `forget()`, preserving the FIFO audio/retirement order established
by `BoundedSpeechPcmIngress`. The worker emits `Bound`, `Retired`,
`Forgot`, or `Error` acknowledgements, each with the source ID and
ingress epoch. The producer must drain these notifications before
draining ASR results and must apply them to the *exact* WAV RecordingId,
never by a reused ODAS source number.

For normal end of source, the producer calls `retire()` and, if
accepted, `SourceRecordingSession::retireDeferred()`. After the worker
acknowledges the ASR generation, the producer calls
`SourceRecordingSession::acknowledgeRetired(recording_id, generation)`.
It then drains ASR results and calls `completeGeneration()` once the
retired generation has no in-flight work. A null generation persists a
WAV-only sidecar. The producer uses `cancelGeneration()` for
discontinuity acknowledgements.

The acknowledgement mailbox has 256 entries by default and applies
**worker-side backpressure** rather than silently dropping required
retirement messages. Stopping/destroying an adapter bypasses mailbox
backpressure so thread destruction cannot deadlock. Call
`closeInput()`, drain acknowledgements until `finished()` and
complete sidecars before destroying it; emergency destruction may lose
unsent notifications, which must be treated as an overload/error.

This is not a hard real-time implementation: producer calls validate
and copy bounded PCM, acquire mutexes, and can allocate. The ASR model
remains on the runtime's own independent worker. Hardware latency,
accuracy and RK3588 deadlines must be measured separately.
