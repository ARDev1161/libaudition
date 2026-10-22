# RC Qt backend validation — evidence matrix

This document distinguishes **actual GUI execution** from “the backend built”
or “the generic Qt self-test passed.” It is based on the
[successful CI #1608](https://github.com/ARDev1161/libaudition/actions/runs/39746849316)
at `ddde13ca02a54c455b0427424add8127c785c50f` (2026-10-22).
That run completed with **25 successful jobs** and one skipped
`real-spatial-corpus` job. It validates **that exact commit only**; later Qt
changes must pass their own CI before release.

## Proven Qt runners on CI #1608

| Workbench feature | Qt CTest evidence | CI job / input | What is actually verified |
|---|---|---|---|
| Core widgets and all top-level tabs | `qt_self_test` | `qt-demo`, `qt6-demo`, optional-backend jobs | Page topology, interactive synthetic DSP/temporal outputs and controls |
| Temporal recurrence/DTW/motifs/streaming and Hungarian cost matrices | `qt_self_test` | `qt-demo`, `qt6-demo` | Synthetic input, assignment edges/penalties and malformed-input handling |
| PCM playback dialog lifecycle | `qt_playback_lifecycle` | `qt-demo`, `qt6-demo` | Repeated opening/closing, normalization and pending callback cleanup |
| Silero VAD | `qt_real_silero` | `sherpa-backend`, pinned ONNX | GUI VAD workflow invokes the actual model |
| Whisper ASR | `qt_real_whisper-real` | `sherpa-backend`, pinned encoder/decoder/tokens | GUI ASR invocation and visible inference result |
| Speaker embeddings, verification and identification | `qt_real_speaker-real` | `sherpa-backend`, pinned speaker ONNX | GUI speaker workflow and enrollment/ID result |
| Piper speech synthesis | `qt_real_piper-real` | `sherpa-tts-backend`, pinned Piper model | GUI TTS creates an output waveform |
| CLAP text/audio similarity | `qt_real_clap` | `clap-backend`, pinned CLAP model/tokenizer | GUI text/audio model inference responds |
| AASIST synthetic-voice detection | `qt_real_aasist` | `aasist-backend`, pinned AASIST ONNX | GUI classification invokes the model |
| WORLD voice analysis | `qt_world` | `world-backend` | GUI analysis produces features |
| GTSAM spatial fusion | `qt_gtsam` | `gtsam-backend` | GUI fusion runs a synthetic geometry scenario |
| libsamplerate | `qt_samplerate` | `samplerate-backend` | GUI resampler writes and reopens converted WAV |
| WebRTC AEC3 | `qt_aec3` | `aec3-backend` | GUI AEC pipeline creates correctly formed WAV |
| ODAS adapter errors / capture reset | `qt_odas_errors`, `qt_alsa_errors` | `odas-backend` | Error, restart and resource lifecycle; **not successful microphone capture** |
| Sherpa async error handling | `qt_sherpa_errors` | `sherpa-backend` and `sherpa-tts-backend` | Nonblocking invalid-input/error states; **not model accuracy** |

The actual CI job logs—not simply the presence of a test name—confirm
`qt_real_silero`, `qt_real_whisper-real`,
`qt_real_speaker-real`, `qt_real_piper-real`,
`qt_real_clap`, `qt_real_aasist`, `qt_world`, and
`qt_gtsam` passed. These use intentionally generated WAVs or pinned test
fixtures and are not representative acoustic quality measurements.

## Not established by this test matrix

- A positive, long-duration **physical ReSpeaker/ODAS/ALSA** recording run.
  The ODAS/ALSA CI jobs exercise failure/recovery without a hardware device.
- End-to-end audio-scene simultaneous multi-speaker separation and calibrated
  DOA tracking in a real room. `real-spatial-corpus` was **SKIPPED**, not green.
- Numerical accuracy of VAD, ASR WER, acoustic tagging, speaker ID, diarization,
  temporal matching, or AASIST in representative field recordings.
- RTX/RK3588 latency guarantees, embedded NPU providers or unattended device
  reliability.
- Positive Qt execution for every *individual* extended Sherpa subpanel
  (e.g. KWS, language ID, denoise, diarization), even though separate
  real-model CTest jobs exercise some underlying APIs. A test of the underlying
  backend is **not** equivalent to testing its full GUI interaction.
- Dedicated positive Qt YAMNet/EfficientAT inference panels. Their backend
  and real-model jobs verify library integration; generic live audio tagging
  and its error path are separately exercised in the Qt acoustic scene.

## Follow-up rules

Run the GUI on a configured device with user-supplied model paths before
asserting physical/human-facing quality. For any newly added Qt controls,
extend `qt_self_test` with a deterministic success path and at least one
error path. Do not block a dependency-light C++ library RC solely because
optional external devices or nonredistributable model weights are absent:
mark such capabilities **not physically validated** and exclude numerical
quality claims. Do not tag the RC while the exact HEAD CI is pending or red.
