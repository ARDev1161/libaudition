# Per-source VAD + ASR (streaming audio, offline utterance recognition)

`audition::SourceSpeechRuntime` lives in the dependency-free pipeline library.
It accepts mono 16-kHz PCM from **each tracked source separately**, builds an
independent `IVadSession` for each active `source_id`, and runs an
`IAsrEngine` on each **completed** speech segment. Inference does not run
on the capture thread: one VAD/segmentation worker and one ASR worker have
bounded queues. This initial stage produces **final** transcripts only,
not incremental token-by-token ASR partials.

```text
              per-ODAS-track mono PCM
                        |
                SourceSpeechRuntime
                        |
       per-source VAD session (fixed frame size)
              /                     \
     silence pre-roll            speech buffer
                                  + post-roll
                                       |
                               completed utterance
                                       |
                          bounded async ASR worker
                                       |
                         SourceTranscriptEvent
                        (source id, start/end PCM
                         sample positions, text)
```

## Interface

```cpp
#include <audition/speech/source_speech_runtime.hpp>

auto vad = std::shared_ptr<audition::IVoiceActivityDetector>(/* backend */);
auto asr = std::shared_ptr<audition::IAsrEngine>(/* backend */);
audition::SourceSpeechRuntime speech{vad, asr};
for (const auto& frame : odas_result.separated_frames) {
    speech.push(frame.track_id.value(), frame.audio.view());
}
for (const auto& transcript : speech.drainTranscripts()) {
    // transcript.source_id is an ODAS SpatialTrackId, not SpeakerId.
    // transcript.transcript.text is the completed utterance.
}
// On any ODAS track retirement, source ID reuse or ALSA discontinuity:
speech.forget(track_id);
```

A valid input is mono interleaved float32 PCM at the configured rate;
chunks of at most one second. The backend-specified VAD frame length is
applied internally (e.g. Silero's 512 samples at 16 kHz), independently of
ALSA or ODAS's 128-sample capture hop.

`SourceSpeechOptions` allows changing pre-roll (200 ms), post-roll
(300 ms), minimum speech (150 ms), maximum utterance (10 s),
source/queue/event capacities and VAD frame length. Short speech is
discarded without ASR; long speech is split at a hard bounded duration.
Dropped chunks invalidate that track's previous generation to avoid
joining speech across missing data. ASR overflows are counted separately.
There is no automatic resampling, cross-source diarization, identification
or acoustic-to-ROS conversion.

**Cancellation:** `forget(id)` clears buffered chunks, completed but
not-yet-recognized segments, and previously published transcripts of that
track. An already-running ASR inference cannot necessarily be aborted,
but the generation check discards its late result. Reusing the numeric
ODAS ID thus starts a new VAD state without inheriting previous speech.

## Qt live demonstration

Build with both `LIBAUDITION_WITH_ODAS=ON` and
`LIBAUDITION_WITH_SHERPA=ON`, alongside the Qt demo option. The
**Acoustic scene** tab has an independent checkbox:

`Transcribe ODAS speech (Silero VAD + SenseVoice offline ASR)`

Choose three files supplied by the respective model publisher:

- Silero `silero_vad.onnx` (Sherpa VAD)
- SenseVoice ONNX model `model.onnx`
- The matching SenseVoice `tokens.txt`

These are **not** bundled or downloaded by this PR; users must check
model provenance and license themselves. The Qt table adds
**Speech / transcript**, with the most recent complete transcription
for each active track. It can coexist with YAMNet classification.
No ASR output appears until the end of a detected utterance plus
post-roll and model inference time.

This is an **offline ASR engine fed by a streaming VAD**, not a
`IStreamingAsrEngine` live-token implementation. Native incremental
ASR partials, model selection beyond SenseVoice in Qt, speaker
identification, and timestamped word alignment are follow-up work.

## Testing

```bash
ctest --test-dir build -R 'SourceSpeechRuntime' --output-on-failure
```

Dependency-free fake VAD/ASR tests cover parallel independent sources,
segment timestamps and pre-roll, a slow in-flight recognition canceled
by `forget()`, reused IDs, source-isolated VAD failures, input format
rejection, and source capacity. The CI matrix also checks compilation
with and without Sherpa/ODAS/Qt; **CI cannot validate transcription
accuracy without the actual model weights or a physical microphone**.

The library boundary remains unchanged: audio-derived events are the
output. ROS 2, camera, radar, TF and robot decisions belong in the
consuming `audio_nav2` application.
