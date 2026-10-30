# WebRTC APM migration

Status: **unified AGC1/AGC2 + AEC3 migration implemented**. The
main CI #1842 and dedicated WebRTC APM CI #39 passed at commit
`f822856`, including installed-package C++17 AEC3 and C++20 AGC1/AGC2
consumers.
The standalone AGC2/AEC3 source, namespace workarounds and their downloader
have been removed. The full-demo configuration has passed CI; Yocto deployment, cross-platform
runtime behavior, audio quality and latency benchmarks remain to be verified.

## Source of truth

Prefer the maintained Freedesktop `webrtc-audio-processing` release, with a
pinned source revision and a Meson-built package for CI/Yocto. WebRTC APM is
not the complete WebRTC transport stack. Use its public AudioProcessing API,
not vendored internal `webrtc::AudioBuffer` types.

## Compatibility contracts

1. Preserve `audition::dsp::ISourceGain`, `makeSourceGain()`, and
   `audition::IEchoCancellerSession` public signatures.
2. An APM instance is scoped to one logical stream. Maintain independent
   state per ODAS speech track when using an AGC-only instance.
3. AEC requires the corresponding render/reference stream. In a full
   capture pipeline run AEC on microphone channels *before* ODAS/SSS;
   never assume separated speech alone is sufficient for echo cancellation.
4. Convert arbitrary input chunks to APM's 10-ms frame cadence without
   dropping samples. Maintain timestamp/epoch alignment and document
   algorithmic latency. Keep an independent true raw capture for benchmarking.
5. Support selectable APM AGC1/AGC2, AEC3, NS and bypass. Avoid running
   AGC1 and AGC2 simultaneously unless deliberately supported.
6. Keep non-WebRTC RMS/Speex gain implementations as comparison baselines.

## Staged replacement and removal

- [x] Add APM 2.x package discovery, automatic Meson ExternalProject fetch
      pinned to Freedesktop commit `bc838790`, and APM integration CI.
- [x] Add opt-in APM-backed AGC1 and AGC2 processors with 10-ms framing,
      source-local state, gain metrics and reset tests.
- [ ] Measure exact APM latency and verify signal quality on real ODAS audio.
- [x] Implement APM-backed AEC3 with 10-ms render/reference frames, stream
      delay, reset and AEC metrics via the public APM API.
- [x] Expose APM noise suppression as a separate configurable opt-in SDK
      capability. Live Qt speech-path integration is implemented; measured
      sound-quality improvements remain open.
- [x] Pass dedicated Linux APM #11 unit tests and Qt AEC3 smoke test.
- [ ] Revalidate combined full-demo with APM and cross-platform Qt 5/6,
      GCC with -Werror, Clang, x86-64 and ARM64.
- [ ] Bench on identical input PCM: RMS/Speex versus APM AGC1/AGC2,
      including WER, clipping, signal/noise levels and CPU cost.
- [x] Remove the standalone `Enaium/webrtc-agc2` and
      `Enaium/webrtc-aec3` implementations, namespace-renaming
      workarounds and `cmake/PatchWebrtcAec3.cmake`.
- [x] Export the installed `audition::backend_webrtc_apm` target and
      `FindWebRTCAPM.cmake`, install matching fetched APM headers and shared
      library, and pass the installed C++17/C++20 consumer checks.
- [ ] Verify Yocto cross-compilation, APM runtime dependencies, and
      the absence of duplicate `webrtc::` implementations in deployed images.

## Dependency evaluation

Freedesktop APM is built with Meson and consumed by libaudition through a
CMake imported target. The source is pinned to post-2.1 commit `bc838790`
for compatibility with current Abseil/GCC. A Git submodule alone would
not solve dependency management or cross-compilation.

## Build with unified APM

CMake first locates a system `webrtc-audio-processing-2`, then fetches the
pinned source if fetching is enabled. On Linux, install Meson, Ninja and the
Abseil development package before building.

```sh
cmake -S . -B build-apm -G Ninja \
  -DLIBAUDITION_WITH_WEBRTC_APM=ON \
  -DLIBAUDITION_WITH_WEBRTC_AEC3=ON \
  -DLIBAUDITION_BUILD_QT_DEMO=ON
cmake --build build-apm --target libaudition_qt_demo
```

## Per-source NS usage (SDK-only)

The generic C++17 interface and exact bypass live in
`<audition/dsp/source_noise_suppressor.hpp>`. When APM is enabled, a
C++20 application can link `audition::backend_webrtc_apm` and include
`<audition/backends/webrtc_apm_noise_suppressor.hpp>`:

```cpp
auto ns = audition::dsp::makeWebRtcApmSourceNoiseSuppressor(
    audition::dsp::NoiseSuppressionLevel::high);
auto cleaned = ns->process(source_pcm.data(), source_pcm.size(), 16000U);
ns->reset(); // source retirement, track-ID reuse, or sample-rate change
```

Levels are low/moderate/high/very-high. Each instance handles one source,
rejects malformed chunks atomically, supports 16/32/48-kHz normalized mono
PCM and keeps the output count unchanged. Ten-millisecond framing initially
emits frame_size - 1 samples of silence; reset discards partial frames.
Process errors reset the instance and raise an exception. Processing
allocates output and must run outside a hard-realtime capture callback.
AGC and AEC are explicitly disabled in the NS session. Keep microphone
PCM fed to ODAS unchanged; NS belongs only on the speech branch after SSS.
Qt acoustic scene provides an off-by-default NS checkbox and four-level
selector. Switching while capturing resets source-local NS state between
ODAS hops, without restarting ALSA/ODAS or retiring WAV recordings. NS
runs on post-SSS speech PCM before optional AGC, VAD and ASR, while ODAS
and classification retain unmodified PCM. The speech WAV records the
post-processing PCM. On preprocessing failure the affected hop becomes a same-length muted
PCM interval (rather than raw unprocessed audio), with a `speech_preprocessing_failure` gap marked in
the WAV's JSON sidecar (distinct from `recorder_backpressure`). The
muted samples are also admitted to VAD/ASR
to preserve source-local sample offsets; the broken NS/AGC instance
is discarded. The SDK helper `processSourceSpeechHopOrMute` exposes
the same explicit fail-closed behavior for consumers, returning the
error and a failed flag without losing the PCM clock.
Qt's diagnostics additionally show per-source **matched** pre-processing
and post-processing RMS/peak levels and their combined dB difference, based
only on successful PCM windows. This is total NS+AGC level change, **not**
a measure of removed noise, intelligibility or SNR. The accumulators reset
on changes to NS/AGC, source retirement, ALSA XRUN and each 20-Hz publication
window. The headless `qt_noise_suppression_controls` test checks Qt controls
without a microphone. Synthetic tests do not establish real-room SNR,
latency or ASR WER improvements.

The AGC dropdown includes `WebRTC APM AGC1` and `WebRTC APM AGC2`.
The AEC3 UI keeps the existing WAV inputs and stream-delay control. The
two custom AEC3 filter-initialization options from the old standalone
library cannot be expressed through APM 2.x's public API; they are
disabled in the Qt UI and explicitly rejected when set programmatically.
AEC3's general filter tuning uses WebRTC's internal defaults.
