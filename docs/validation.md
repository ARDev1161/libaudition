# Validation status

## v0.5 release validation

The v0.5 release line is validated in CI with:

- strict C++17 builds on Ubuntu and macOS,
- warnings-as-errors for libaudition targets,
- dependency-free/minimal configuration coverage,
- GoogleTest unit and deterministic scenario suites,
- AddressSanitizer + UndefinedBehaviorSanitizer coverage for the default dependency-free build,
- clean install/export followed by a separate C++17 downstream consumer using
  `find_package(libaudition 0.5.0 EXACT CONFIG REQUIRED)`,
- dedicated backend build/test/install/downstream-consumer jobs for ODAS,
  libsamplerate, WebRTC AEC3, sherpa speech, sherpa TTS, CLAP, AASIST, WORLD,
  and GTSAM.

The default installed-package smoke consumer includes
`<audition/audition.hpp>` and links the public non-backend targets:

- `audition::core`,
- `audition::interfaces`,
- `audition::dsp`,
- `audition::spatial`,
- `audition::memory`,
- `audition::pipeline`.

It constructs representative v0.5 public components including
`TemporalSpatialTrackSmoother`, `AcousticEventAssembler`, and
`InMemoryAcousticSourceRegistry`, so the check verifies both headers and
installed link interfaces rather than only package discovery.

## Deterministic acoustic scenarios

The test suite covers the core v0.5 semantics, including:

- persistent `AcousticSourceId` continuity across transient tracker handoff,
- simultaneous acoustic sources remaining distinct,
- `AcousticEvent` lifecycle and track handoff,
- calibrated dBFS -> dB SPL -> source-level acoustic range prior,
- temporal range/position moment and covariance propagation,
- degenerate spatial geometry,
- robust handling of conflicting/outlier spatial observations,
- clock-domain and timestamp-order safety.

These tests are deterministic library validation. They do not replace
real-room recorded-fixture or hardware-in-loop validation.

## Optional backend validation

Each optional backend has its own CI job so backend dependencies do not weaken
the dependency-free core contract. Backend jobs compile the adapter, run its
tests, install the package to a clean prefix, and build a separate downstream
consumer against the installed CMake targets.

Model-backed inference jobs use pinned fixture artifacts and verify their hashes
before tests where applicable.

## Hardware and recorded-room follow-up

Recorded ReSpeaker/real-room fixtures, microphone SPL calibration measurements,
and end-to-end deployment through a consuming application such as
`audio_nav2` remain integration validation rather than blockers for the
deterministic v0.5 library release.

The library boundary remains acoustic-only; ROS 2, TF, robot state, camera,
radar, and cross-modal fusion are deliberately outside this validation scope.
