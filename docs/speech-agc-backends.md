# Speech gain backends (Qt acoustic scene)

Gain processing is **source-local**: ODAS separation → selected AGC →
Silero/TEN VAD and offline ASR. The source WAV records the **post-AGC**
PCM delivered to speech ingress when AGC is enabled; otherwise it stores
the unmodified ODAS PCM. Keep a separate raw ODAS capture for controlled
comparisons, since the normal source WAV no longer provides both signals.

| Backend | Status | Qt selection | Dependency |
| --- | --- | --- | --- |
| RMS AGC | Built-in | `RMS AGC` | None |
| SpeexDSP AGC | Optional | `SpeexDSP AGC` | SpeexDSP development package |
| WebRTC APM AGC1 | Optional | `WebRTC APM AGC1` | Unified WebRTC APM 2.x |
| WebRTC APM AGC2 | Optional | `WebRTC APM AGC2` | Unified WebRTC APM 2.x |

## Unified WebRTC APM

The library uses **one pinned Freedesktop WebRTC Audio Processing Module**
implementation for AGC1, AGC2 and AEC3. It no longer fetches the independent
`Enaium/webrtc-agc2` or `Enaium/webrtc-aec3` extractions. This prevents
duplicate, incompatible implementations of internal `webrtc::` classes.

Choose `-DLIBAUDITION_WITH_WEBRTC_APM=ON` (or enable
`LIBAUDITION_WITH_WEBRTC_AEC3`, which implies APM). Build dependencies
include Meson, Ninja and Abseil development headers. CMake first attempts
to find an installed `webrtc-audio-processing-2` package, and can fetch
the pinned APM source when `LIBAUDITION_FETCH_DEPENDENCIES=ON`.
The system's old `webrtc-audio-processing-0.3` is not API-compatible.

When APM is enabled, WebRTC APM AGC2 is the default Qt speech AGC.
AGC1 and AGC2 run in separate, per-source APM sessions; both use 10-ms
frames with a buffering delay when ODAS emits shorter hops. They operate
only on speech gain: AEC3 requires both the unseparated microphone input
and playback reference and belongs before ODAS.

The reported `gainDb()` is the energy ratio measured per processed
frame, not an internal WebRTC gain target.

## SpeexDSP

On Ubuntu/Debian install `libspeexdsp-dev`. Configure with
`-DLIBAUDITION_DEMO_WITH_SPEEXDSP_AGC=ON` (the default) and CMake
auto-detects it. The adapter maintains a separate mono preprocessing
state per tracked source and converts normalized float PCM to/from
signed 16-bit samples. Denoising remains disabled to allow comparing
gain algorithms without adding noise reduction.

Its 10-ms framing can introduce a delay of up to one frame for shorter
input chunks. Sample-accurate applications must account for this delay.

## Evaluation before release

Measure RMS, peak, clipping, speech/noise levels, latency, CPU use and
ASR WER on **identical input PCM** with fixed VAD boundaries. Neither
gain processor recovers information already lost during ODAS separation,
and increased loudness alone does not prove improved ASR accuracy.
