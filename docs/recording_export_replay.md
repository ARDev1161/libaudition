# Recording export and replay (C++17, core-only)

This worked example exercises **real file output and re-import** through the
public libaudition WAV/recording-timeline APIs. The PCM and annotations are
synthetic; no microphone, ODAS geometry, ASR weights, or Qt are required.

## Build and run

```sh
cmake -S . -B build-recording -DCMAKE_BUILD_TYPE=Release \
  -DLIBAUDITION_BUILD_EXAMPLES=ON -DLIBAUDITION_BUILD_TESTS=ON
cmake --build build-recording --target libaudition_recording_replay_cli
./build-recording/examples/libaudition_recording_replay_cli --export "$PWD/recording-demo"
./build-recording/examples/libaudition_recording_replay_cli --replay "$PWD/recording-demo"
ctest --test-dir build-recording -R '^cli-recording-export-replay-smoke$' --output-on-failure
```

The exporter writes four files to the chosen directory (overwriting the two
named WAV/sidecar pairs if they exist):

| File | Content |
| --- | --- |
| `source-generation-101.wav` | Mono PCM16, 16 kHz, 20 frames: 8 positive samples, 4 silence samples, 8 negative samples |
| `source-generation-101.wav.json` | Recording ID 101; synthetic sound annotation [0, 8); gap [8, 12) with `recorder_backpressure` reason |
| `source-generation-102.wav` | Mono PCM16, 16 kHz, 8 negative samples |
| `source-generation-102.wav.json` | Recording ID 102; new sound annotation [0, 8), no inherited gap |

The same numeric track ID `7` is intentionally reused for the second
recording. **Recording IDs, not ephemeral source IDs, own sidecars and sample
offsets.** All intervals are half-open and relative to the first sample of
their own WAV. They cannot be joined across a retired/reused source generation.

The export calls `StreamingPcm16WavWriter::append()`,
`appendSilence()`, `SourceRecordingSession::addSound()`, `addGap()` and
`retire()`. The replay independently calls `loadWav()` and
`readRecordingTimelineJson()`, then checks format, frame counts, gap
samples, annotation identity and rate agreement. The CTest self-test also
modifies a valid JSON sidecar to claim 8 kHz and proves replay rejects it.

**Gap policy:** in this example, missing samples are intentionally written as
zeros to preserve the timeline. A different capture application may instead
segment or discard its recording; it must explicitly define that mapping and
must not silently treat a gap as measured silence. The sidecar describes the
reason; zero-filled PCM alone does not.

**Not demonstrated:** live ALSA capture, asynchronous VAD/ASR generation
retirement, device clock synchronization, recording recovery after a crash,
calibrated classification accuracy, or embedded real-time deadlines. Those
remain separate acceptance tests.
