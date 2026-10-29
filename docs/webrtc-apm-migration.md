# WebRTC APM migration

Status: **unified AGC1/AGC2 + AEC3 migration implemented**. The
dedicated WebRTC APM CI #11 passed the AGC and AEC3 functional tests.
The standalone AGC2/AEC3 source, namespace workarounds and their downloader
have been removed. Main full-demo CI, cross-platform and quality benchmarks
must be revalidated before release.

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
- [ ] Expose APM noise suppression as a separate configurable capability.
- [x] Pass dedicated Linux APM #11 unit tests and Qt AEC3 smoke test.
- [ ] Revalidate combined full-demo with APM and cross-platform Qt 5/6,
      GCC with -Werror, Clang, x86-64 and ARM64.
- [ ] Bench on identical input PCM: RMS/Speex versus APM AGC1/AGC2,
      including WER, clipping, signal/noise levels and CPU cost.
- [x] Remove the standalone `Enaium/webrtc-agc2` and
      `Enaium/webrtc-aec3` implementations, namespace-renaming
      workarounds and `cmake/PatchWebrtcAec3.cmake`.
- [ ] Update installed CMake exports, documentation and Yocto packaging;
      verify no duplicate `webrtc::` runtime implementations.

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

The AGC dropdown includes `WebRTC APM AGC1` and `WebRTC APM AGC2`.
The AEC3 UI keeps the existing WAV inputs and stream-delay control. The
two custom AEC3 filter-initialization options from the old standalone
library cannot be expressed through APM 2.x's public API; they are
disabled in the Qt UI and explicitly rejected when set programmatically.
AEC3's general filter tuning uses WebRTC's internal defaults.
