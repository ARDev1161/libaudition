# Per-source speech runtime (foundation)

`PerSourceSpeechRuntime` is a dependency-light, C++17 pipeline for separated
mono PCM streams, keyed by an external acoustic track ID. It does **not**
depend on ODAS, sherpa-onnx, ROS or Qt. Applications supply any
`IVoiceActivityDetector` (which creates an independent `IVadSession`
per source) and an `IAsrEngine` held by `std::shared_ptr<const ...>`.

The caller supplies source-local mono samples through
`push(track_id, AudioView)` at the configured sample rate (16 kHz by
default). **`push()` runs VAD synchronously while holding the runtime
mutex**; it is not a nonblocking capture-thread guarantee. For devices
with strict audio callback deadlines, invoke it from an application-owned
processing worker fed by a bounded input queue and explicitly reset on
lost/discontinuous PCM. Active VAD frames are concatenated until the VAD
first becomes inactive. Completed
speech with at least `min_speech_samples` is moved into the bounded job
queue. The ASR worker transcribes complete segments off the capture thread.
The application drains results with `takeResults()`.

Safety and lifecycle:
- Independent VAD state and PCM accumulation for each source;
- At most `max_sources` active source sessions and `max_pending_segments`
  waiting jobs/results. Unreleased retired generations are separately capped
  by `max_retired_generations` (default 64);
- `max_segment_samples` caps speech buffers to prevent unbounded growth;
- `max_input_chunk_samples` bounds synchronous `push()` work: zero resolves
  to one second at the configured sample rate (16,000 mono samples by
  default), and smaller nonzero limits are supported. Oversized chunks
  raise `std::length_error` before VAD or source-state mutation. Split
  longer input into contiguous blocks, and reset/forget on actual PCM loss;
- `forget(track_id)` invalidates pending/inflight results from retired IDs;
- `reset()` invalidates source generations and jobs after a discontinuity;
- Per-runtime `statistics()` reports completions, losses, last ASR error,
  the current unread transcript mailbox size (`buffered_results`), and
  the number of evicted unread transcripts (`evicted_results`).
  The output mailbox is independently bounded by `max_pending_segments`;
  oldest unread results are discarded on overflow. The aggregate
  `dropped_segments` includes queue failures and those evictions.
  `evicted_results` excludes explicit cancellation via `forget()`/`reset()`.
- Completed ASR outputs have a byte budget (16 KiB) and combined token/word
  item budget (512) by default. Oversized outputs are dropped and counted;
  these limits apply after model inference and do not bound backend memory.

This is an **initial foundation**, not the complete low-latency ASR pipeline:
VAD gates whole pushed chunks with configurable pre-roll and post-roll, but without
partial transcripts or independent phoneme-precise VAD onset estimates. It is suitable
for mocked/controlled tests, not a claim of on-device recognition quality.
Configurable pre/post-roll, the ODAS/Qt adapter, track-local error reporting,
and a per-source transcript column are now implemented. Remaining gaps are
hardware-verified phrase onset/end, ASR latency instrumentation, and recognition
quality under simultaneous speech. See
[ReSpeaker live acceptance](respeaker_live_speech_acceptance.md).
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

## Automatic Qt demo speech assets

When `LIBAUDITION_BUILD_QT_DEMO=ON` and `LIBAUDITION_WITH_SHERPA=ON`,
`LIBAUDITION_DEMO_FETCH_SPEECH_MODELS=ON` (default for demo builds) downloads
Silero VAD ONNX and sherpa-compatible SenseVoiceSmall INT8 ONNX plus its
matching token table at **CMake configure time**, caching them under
`<build>/demo-models/speech/`. Pre-filled paths enable the Acoustic scene
transcription checkbox by default; sound classification remains independently
controlled. The other top-level Qt demo tabs also use scroll areas.

This first-time setup requires internet access and roughly 240 MB of model
data (excluding the rest of sherpa-onnx dependencies). Subsequent CMake
configures reuse the cached models; SenseVoice and tokens are SHA256 verified.
For offline configurations set `-DLIBAUDITION_DEMO_FETCH_SPEECH_MODELS=OFF`
and provide paths manually in the Qt demo. Automatic downloads never happen
in the libaudition library itself or in a dependency-only build.

Models have separate third-party licenses: check the respective upstream
model repositories before redistribution. Model download success and actual
ReSpeaker recognition quality still require local verification.

## Qt live source history and playback (experimental)

The live Acoustic scene keeps retired ODAS source rows in the **Tracked sources**
table, displayed in gray rather than removed. Each source with separated SSS
PCM exposes a **Play** button that opens a small player with Play/Pause, seek,
volume and elapsed/total time. The sphere continues to show only live tracks.

Source PCM is stored as mono 16-bit WAV at the negotiated sample rate under
`<system temp>/libaudition-recordings/session-*/track_*.wav`, separate from ASR
segment recording. The recorder caps PCM at **256 MiB per capture session**;
after the cap, later audio is not recorded. Captures are retained on disk
until the user deletes the temp directory (the demo does not upload speech).
Files are not automatically erased when a track disappears. Active WAV headers
are refreshed approximately every second, but finished files are more reliable
for playback. Playback is available when **Qt Multimedia** development support
was found at configure time; otherwise the demo builds without it and displays
an explanation in the popup. This is a diagnostic recorder; disk I/O in the
capture thread should be stress-tested before claiming hard real-time behavior.

Current limitations: track IDs are the history keys (ID reuse within a capture
session can replace a previous table entry), and starting a new capture resets
the visible table history while leaving previous WAV files on disk. A future
iteration should use a generation-qualified identity for the recordings,
publish recorder drop counts, and move disk writes onto a bounded background
worker. No audio or transcript is sent to an external service by the recorder.

## Time-aligned signal level diagnostics

The live Qt Acoustic scene reports raw ALSA input channel RMS/peak levels
and each ODAS separated source's RMS/peak in the **same UI reporting window**
(roughly 50 ms). The raw channels include non-microphone channels; compare
only channels selected in the mic mapping. SSS values are keyed by track ID
and include the separated sample count. Because sources may start/end inside
a reporting window, the windows are contemporaneous but are not guaranteed
to contain precisely the same number of samples. RMS is measured across the
available PCM of each source, including silence.

For each source the GUI also displays a *projected* gain:
`clamp(-3 dBFS - source_peak, 0, 40 dB)` and resulting peak. This is a
diagnostic calculation **only**: raw capture, separated WAV files, VAD,
tagging and ASR remain unchanged. A zero sample source is omitted.

The current ODAS backend default is
`sss.separation_mode=DelayAndSum`, `sss.output=Separated`,
`sss.post_filter_mode=MultiSource`. Consequently the recorder receives
the *Separated* signal, not the *Postfiltered* signal. Do not interpret
postfilter settings as evidence of gain reduction on the currently selected
output without checking the backend processing path.

## Library peak normalization and annotation timeline

The public `<audition/audio/peak_normalizer.hpp>` API offers
`audition::peakNormalizePcm16` with target peak -3 dBFS, maximum boost 40 dB,
and no attenuation by default. It operates on an entire PCM16 sample vector
and returns a separate sample vector plus measured gain and peaks. This is
**offline peak normalization**, not adaptive AGC or acoustic noise reduction;
it does not modify the input or improve signal-to-noise ratio. The Qt
recording player uses this library API to create its playback-only WAV copy.

The public `<audition/audio/recording_timeline.hpp>` introduces
`RecordingTimeline`, `TimelineSpeechEvent` and `TimelineSoundEvent`.
Events use recording-relative sample offsets and independent lanes for speech
text and sound class predictions. Distinct `RecordingId` values must identify
distinct generations of reused ODAS track IDs. This commit establishes
the in-memory data model and validation; **persistent JSON export and
attaching live ASR/tagger results to the timeline are follow-up integration
work**, not yet supplied by the new API.

## Per-source recorded annotations

The Qt live recorder now writes a versioned `*.wav.json` sidecar for each
source WAV, through the public
`audition::recordingTimelineToJson` / `writeRecordingTimelineJson` APIs.
The JSON carries sample rate, recording generation ID, speech segments and
classifier windows as two independent tracks. Speech offsets come from
ASR segment capture timestamps, not the time inference completed. Tagging
windows use the library classifier's actual end-sample and window length,
rather than approximating them from UI refresh time; re-reading a completed
window does not duplicate it. Sources retired and reused by ODAS are written
to separate sidecars.

The Qt popup shows independently labeled sound-event and transcript timeline
lanes sharing the media player's position. Both move as audio plays; click a
location on either lane to seek. Peak normalization remains playback-only;
timestamps remain relative to the **original WAV**.

Important limits: SenseVoice generally supplies utterance-level text, not
word-aligned timing, so speech labels cover genuine VAD/ASR segments without
inventing per-word offsets. The asynchronous `retire()` drains final ASR utterances by generation,
although shutdown still applies a 10-second deadline. Audio dropped by the bounded recorder also means inference
sample offsets may diverge from the physically written WAV. The JSON is written on retirement or at shutdown, not continuously streamed
for active tracks. The installed library supports strict C++17 JSON loading
via `audition::readRecordingTimelineJson`; Qt reads this typed API.

## Draining ASR when an ODAS track retires

`PerSourceSpeechRuntime::retire(track_id)` now flushes any buffered
minimum-length speech and queues ASR without blocking the capture producer.
Completed jobs retain their generation identity even if ODAS immediately reuses
the same numeric track ID. `pendingResults(generation)` tells the consumer
when the generation has no remaining queued/in-flight inference. The Qt adapter
uses `drainReady()` to atomically collect results and finished generations;
this avoids dropping an ASR result in the gap between separate polling calls.
`activeGeneration(track_id)` validates current-source identity across reused
numeric IDs. After consuming results and final sidecar persistence the consumer must call
`releaseRetired(generation)` to free retirement state. At the configured
`max_retired_generations` cap, `retire(track_id)` returns `std::nullopt`
**without modifying the active track or queuing its final segment**.
Applications must check this return value, drain finished generations and
release them before retrying, or explicitly `forget(track_id)` if audio can
be discarded. `statistics().retired_generations` exposes the current
metadata pressure. A caller that ignores rejected retirement cannot claim
that its final utterance or sidecar was persisted.

The Qt/ALSA demo now first drains completed ASR generations and retries
`retire()` once when its retirement table is full. If earlier in-flight
ASR still prevents retirement, it explicitly `forget()`s the vanished
track so its state cannot survive into a reused numeric ODAS ID. The
WAV/timeline sidecar persists without the unfinished final transcript,
and live diagnostics increment `retire_capacity_drops`. This is an
explicitly reported overload fallback, **not** lossless ASR retirement.

Persisting a recording after this fallback now removes its obsolete
`generation → recording` bindings, even if `bindAsr()` ran before the
failed retirement. `SourceRecordingSession::boundAsrGenerations()` exposes
binding pressure for diagnostics. Repeated fallback sidecar cycles must
return that count to zero.

The Qt capture adapter delays the retired recording's final JSON write until
that generation has drained; ALSA xrun retirement uses the same mechanism.
On capture shutdown it allows up to 10 seconds for outstanding ASR results
before writing final sidecars; long-running inference may still be lost at
this deadline. The existing `forget()` and `reset()` remain cancellation
APIs for callers that intentionally discard unfinished work.

## Recording subsystem extraction (PR #55 architecture review)

Reusable modules **in installed library**, not the demo:

- `audition::io`: load/save WAV, `StreamingPcm16WavWriter`,
  `normalizePcm16Wav()` (two-pass bounded-memory normalization),
  plus versioned timeline JSON reader/writer.
- `audition::recording::SourceWavRecorder`: independent PCM16 disk
  worker, bounded queue budget and per-source recording generations.
  The default 256-MiB budget is **queued audio**, not a 256-MiB maximum
  lifetime/session size. When a chunk is dropped under backpressure, its
  sample-time interval is zero-filled in the WAV and represented as a
  `gaps[]` annotation. The standard RIFF 4-GiB per-file ceiling remains.
- `audition::recording::SourceRecordingSession`: generation-qualified
  ASR results, independent sound-window and speech timelines,
  audio-gap annotations, sidecar persistence and deduplication.
  Errors saving a sidecar are reported through `persistenceError()`,
  without aborting the live source capture.
- `audition::pipeline::SourceTrackLifecycle`, `audition::dsp::StreamingLevelMeter`
  and `audition::makeFastRefinedWindowPolicy`.
- `SourceClassificationRuntime::takeCompleted()` and progressive
  equivalent provide actual completed inference windows independently of
  GUI repaint intervals. Returned event queues are bounded; completed
  events may be discarded if the client cannot drain them fast enough.

The Qt demo now owns only host ALSA capture, model-specific setup,
`QMediaPlayer`, synchronized lane rendering, and playback interaction.
Its WAV normalization runs via `QtConcurrent` rather than blocking GUI.

A mono source that remains active beyond the RIFF maximum must be retired/
rotated by the host before reaching the 4-GiB file limit. Out-of-disk errors
and a process crash can still prevent the very last annotation save;
full periodic journal durability and hardware SIGSEGV verification remain
explicit acceptance work, not guarantees of this refactor.
