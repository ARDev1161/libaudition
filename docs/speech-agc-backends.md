# Speech gain backends (Qt acoustic scene)

The gain processor is source-local and is applied **after ODAS separation** and
**before Silero/TEN VAD and offline ASR**. The original recording WAV retains
the unprocessed ODAS samples.

| Backend | Status | Selection | Dependency |
| --- | --- | --- | --- |
| RMS AGC | Available | `RMS AGC` | None |
| SpeexDSP AGC | Optional | `SpeexDSP AGC` | SpeexDSP development package |
| WebRTC AGC2 | Not integrated | Not shown | Separate upstream integration needed |

## SpeexDSP

On Ubuntu / Debian, install `libspeexdsp-dev`. Configure the Qt demo with
`-DLIBAUDITION_DEMO_WITH_SPEEXDSP_AGC=ON` to expose the SpeexDSP selector.
The option defaults to ON; CMake auto-detects SpeexDSP and enables it when installed. If the dependency is unavailable, the Qt demo still builds with RMS AGC only. SpeexDSP is selected by default when available.

The adapter uses independent preprocessor instances per tracked source,
10-ms frames, mono int16 samples, and a maximum requested gain of 36 dB.
It converts the ODAS float PCM to int16 for the SpeexDSP API and back. The
output stream is delayed by up to one frame when source chunks are shorter
than 10 ms; the initial unfilled samples are zero. **The adapter must not be
selected in pipelines requiring sample-accurate timing without compensating
for this buffering delay.** Denoising is disabled to compare gain algorithms
without introducing a second noise reduction variable.

The displayed gain is obtained from the SpeexDSP AGC gain query. The existing
RMS implementation remains the baseline for A/B listening and transcription
comparison. Neither backend can reconstruct speech information removed by
source separation, nor do they eliminate Whisper decoding hallucinations.

## Next integration

WebRTC AGC2 is not equivalent to the existing WebRTC AEC3 backend. A separate
AGC2 dependency, packaging/feature gate, 10-ms frame adaptation, platform CI
and timing tests are needed before making it selectable.
