# ReSpeaker live speech acceptance (Silero + SenseVoice)

This runbook validates **application integration**, not model accuracy certified by CI.
Run on an x86 Linux machine with a ReSpeaker USB 4-Mic v2.0, using PR #55
(`feat/per-source-speech-runtime`). Do not merge either stacked PR until
the results have been reviewed.

## Build

From the repository root:

```bash
cmake -S . -B build-speech-live -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DLIBAUDITION_BUILD_QT_DEMO=ON \
  -DLIBAUDITION_WITH_ODAS=ON \
  -DLIBAUDITION_WITH_SHERPA=ON \
  -DLIBAUDITION_BUILD_TESTS=ON
cmake --build build-speech-live --parallel
ctest --test-dir build-speech-live --output-on-failure
```

Check local dependency and model configuration in the CMake output.
Use locally acquired compatible **Silero VAD ONNX**, **SenseVoice ONNX**,
and the corresponding **tokens.txt**; the GUI deliberately does not
download any model.

## Hardware preflight

```bash
arecord -l
arecord -L
```

Identify the correct capture-only `hw:CARD,DEVICE` interface in the
Qt Acoustic scene device picker. The demo requests **6 channels,
16 kHz, signed 16-bit interleaved PCM** and uses microphone channels
1–4 (zero based, leaving the reference channels aside).
If the hardware does not expose this exact format, capture will fail
rather than silently resample. Fix the device/configuration; do not
interpret format errors as VAD/ASR failures.

The ReSpeaker array profile should be selected when using that hardware.

## Live session

1. Launch `build-speech-live/demo/qt/libaudition_qt_demo`.
2. In the Acoustic scene, select the ReSpeaker capture device and enable
   **Transcribe separated sources (Silero + SenseVoice)**.
3. Supply the paths to Silero ONNX, SenseVoice ONNX and its matching
   `tokens.txt`. Start live capture.
4. First stay silent for 10 seconds, then speak a short phrase at normal
   volume and stop. Repeat short / long phrases, including a 20+ second
   monologue. Observe the **VAD state**, **Transcript**, **ASR queue**,
   **completed/dropped counters**, and errors.
5. Use two speakers at distinct azimuths, first alternating, then with
   overlap. Track whether phrases are associated with the correct
   physical speaker/track ID. Do not count an ODAS tracking failure as
   an ASR decoding failure.
6. Stop and restart capture; unplug/replug if safe. Ensure no old
   transcript is attributed to a new track after an ID is reused.
7. Repeat with source classification disabled, then enabled, to detect
   contention with YAMNet / the configured tagging backend.

## Record each failure separately

| Check | Observable expected behavior | Failure classification |
| --- | --- | --- |
| First phoneme | Start of phrase appears in complete transcript | VAD onset / pre-roll / SSS |
| Short pause | Text appears after end-of-speech | VAD end / post-roll / ASR queue |
| Long speech | Bounded segmentation, no unbounded growth | Segment boundary / overflow |
| Silence | No spurious speech text | VAD false positive / model hallucination |
| Two alternating speakers | Transcript belongs to the correct track | Tracking / attribution |
| Overlap | Degradation is noted per source | ODAS separation / ASR |
| Reappearing track ID | No previous-generation transcript appears | Lifecycle cancellation |
| Load | Queue stays bounded and capture keeps running | Worker contention / backpressure |

For every run, save: git SHA, device ID, hardware format, ONNX model
filenames/versions, language, number/positions of speakers, whether
classification was on, ASR counts/drops/errors, any ALSA recoveries,
and a transcript-versus-ground-truth table. Record **wall-clock**
end-of-speech-to-text delay with a stopwatch or screen recording for now:
the current UI counters do **not** provide trustworthy per-utterance
timestamped latency measurements.

Suggested provisional performance targets (engineering goals, **not**
guarantees): no capture interruption; zero stale ID attributions;
bounded pending queue; no missing PCM across max-segment split.
Measure first-phoneme recall and end-of-speech-to-text latency
empirically before selecting numerical acceptance thresholds.

## Important interpretation caveats

- ODAS may produce changing or duplicate-looking tracks, especially
  when sources move or speech overlaps. Confirm track identity before
  concluding that ASR misattributed a transcript.
- A shared offline ASR worker may show longer queue delay for multiple
  simultaneous sources. Queue latency and decode latency must ultimately
  be measured separately.
- This test does not evaluate RK3588 performance; x86 results cannot
  be extrapolated to the target SBC.
- Do not upload raw speech or transcripts from other participants to CI.
  Use consented local recordings if a reproducible audio fixture is needed.

## Follow-up engineering gate

Only after a real-hardware baseline, add monotonic timestamps and a
per-utterance JSONL capture report (VAD onset/end, segment enqueue,
ASR start/finish, track generation, drop reason), then replay known
recordings deterministically with measured first-phoneme preservation,
attribution, latency percentiles and queue pressure. Keep instrumented
capture/reporting in the Qt demo or an optional CLI, not in the
dependency-light speech core's hardware boundary.
