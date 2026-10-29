# WebRTC APM migration

Status: **in progress**. The opt-in APM 2.x AGC1/AGC2 source-gain adapter,
Qt selector, unit tests, and dedicated source-build CI job are implemented.
The AEC3 replacement and deletion of standalone dependencies are still pending.
Do not remove the working AEC3 and AGC2 backends before their replacements
have passed all corresponding tests.

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

- [x] Add APM 2.x pkg-config/system discovery and a dedicated CI job that
      builds the pinned Freedesktop 2.1 source with Meson.
      A production-wide fetch/install integration is still pending.
- [x] Add opt-in APM-backed AGC1 and AGC2 processors with 10-ms framing,
      source-local state, gain metrics and reset tests.
- [ ] Measure exact APM latency and verify signal quality on real ODAS audio.
- [ ] Implement APM-backed echo canceller (including render reference and
      stream delay) and noise suppression; preserve relevant metrics.
- [ ] Run Qt demonstration and AEC functional self-tests on Linux Qt 5/6,
      GCC with -Werror, Clang, x86-64 and ARM64.
- [ ] Bench on identical input PCM: RMS/Speex versus APM AGC1/AGC2,
      including WER, clipping, signal/noise levels and CPU cost.
- [ ] Remove `Enaium/webrtc-agc2` FetchContent and
      `Enaium/webrtc-aec3` ExternalProject, namespace-renaming
      workarounds and `cmake/PatchWebrtcAec3.cmake` only after green CI.
- [ ] Update installed CMake exports, documentation and Yocto packaging;
      verify no duplicate `webrtc::` runtime implementations.

## Dependency evaluation

Freedesktop APM 2.1 is a distribution-oriented Meson project, whereas the
separate third-party AGC2/AEC3 extractions build through CMake. Prefer the
former for the long-term API and predictable shared WebRTC ABI; preserve
pinning and explicit dependency options. A Git submodule alone does not
solve Meson/CMake integration or cross-compilation.

## Try the APM gain backend locally

Install WebRTC Audio Processing **2.1** (the Ubuntu 24.04 apt package is
0.3.1 and does not meet this requirement). Supply its install prefix through
`CMAKE_PREFIX_PATH` and `PKG_CONFIG_PATH`, and configure:

```sh
cmake -S . -B build-apm -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/apm-prefix \
  -DLIBAUDITION_WITH_WEBRTC_APM=ON \
  -DLIBAUDITION_DEMO_WITH_WEBRTC_AGC2=OFF \
  -DLIBAUDITION_BUILD_QT_DEMO=ON
cmake --build build-apm --target libaudition_qt_demo
```

The gain dropdown contains `WebRTC APM AGC1` and `WebRTC APM AGC2`.
With APM available, AGC2 becomes the Qt default. Leave the legacy standalone
AEC3 enabled only for existing configurations until the APM echo adapter is
ready; both engines should not be assumed to share signal-processing state.
