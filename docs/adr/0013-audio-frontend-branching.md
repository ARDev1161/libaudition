# ADR-0013: keep spatial and speech frontends as explicit branches

**Status:** accepted

## Context

Robot audition needs both phase-coherent microphone-array processing and
speech-oriented conditioning. Reference-based echo cancellation, denoising and
speech enhancement are useful before ASR but can modify signals in ways that
invalidate microphone-array phase assumptions.

Resampling and channel calibration are also stateful operations and must not be
hidden inside unrelated inference backends.

## Decision

- Provide explicit channel routing/calibration as a dependency-free DSP block.
- Keep the raw/calibrated array path available directly to spatial backends.
- Apply AEC3 only when an application explicitly composes it into a speech path.
- Require the application to supply the playback/reference stream and stream delay.
- Expose measured quality metrics without fabricating SNR/noise estimates.
- Implement resampling through the existing `IAudioResampler` session contract.
- Keep libsamplerate and WebRTC AEC3 optional and hide their native types.
- Preserve the C++17 public API even when an implementation dependency needs C++20.

## Consequences

Applications can construct different frontend graphs for spatial perception,
speech intelligence, recording and diagnostics without coupling those paths.
AEC cannot accidentally corrupt the ODAS array stream. Backend-specific frame
requirements remain visible and testable.
