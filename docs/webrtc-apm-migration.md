# WebRTC APM migration

Status: **in progress**. Do not remove the working AEC3 and AGC2 backends
before their APM replacements have passed all corresponding tests.

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

- [ ] Configure one APM implementation through the public headers, via a
      version-pinned Meson ExternalProject and/or a system pkg-config package.
- [ ] Implement new APM-backed gain processor, input-level and latency
      handling; cover reset, chunk boundaries, silence and several sources.
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
