# Speech gain backends (Qt acoustic scene)

The gain processor is source-local and is applied **after ODAS separation** and
**before Silero/TEN VAD and offline ASR**. The original recording WAV retains
the unprocessed ODAS samples.

| Backend | Status | Selection | Dependency |
| --- | --- | --- | --- |
| RMS AGC | Available | `RMS AGC` | None |
| SpeexDSP AGC | Optional | `SpeexDSP AGC` | SpeexDSP development package |
| WebRTC AGC2 | Optional SDK adapter | `WebRTC AGC2` | Standalone `webrtc_agc2` library |

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

WebRTC AGC2 is distinct from the existing WebRTC AEC3 backend. The optional
adapter and discovery gate are present; validation of the upstream SDK build,
platform CI and audio timing are still outstanding.

## WebRTC AGC2 (standalone extraction)

This integration targets the pinned API layout in
[Enaium/webrtc-agc2](https://github.com/Enaium/webrtc-agc2), revision
`aa5d2359c357bc15495881c12c52874c8830af0a`. The Qt demo now downloads and builds this pinned dependency automatically by default. No manual root path is required. For an existing locally built copy, configure with:

```bash
cmake -S . -B build -DLIBAUDITION_DEMO_WITH_WEBRTC_AGC2=ON \
  -DLIBAUDITION_WEBRTC_AGC2_ROOT=/path/to/webrtc-agc2
```

The default is `LIBAUDITION_DEMO_WITH_WEBRTC_AGC2=ON` and `LIBAUDITION_DEMO_FETCH_WEBRTC_AGC2=ON`. CMake FetchContent downloads the pinned source and builds it as part of the Qt demo, sets include/link paths automatically, and preselects WebRTC AGC2 in the Qt acoustic scene. Offline builds can disable fetching with `-DLIBAUDITION_DEMO_FETCH_WEBRTC_AGC2=OFF`, use a prebuilt root, or disable the backend with `-DLIBAUDITION_DEMO_WITH_WEBRTC_AGC2=OFF`. Builds with Qt Demo disabled do not fetch AGC2. Unlike WebRTC AEC3, this
is a distinct dependency.

The AGC2 adapter uses the upstream internal voice activity detector,
adaptive digital gain (36 dB maximum), disabled microphone hardware gain
controller, and 10-ms mono FloatS16 processing frames. Initial framing
latency and sample alignment must be accounted for in precision timing
applications. The `gainDb()` diagnostic reports measured input/output
frame-energy gain, not AGC2's internal target level. The runtime comparison
with ODAS + ASR must still be validated on real hardware.
