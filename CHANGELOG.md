# Changelog

All notable changes to this project will be documented in this file.

The project follows Semantic Versioning after `1.0`; the `0.x` public API may evolve.

## [Unreleased]

### Changed

- Renamed the project/package from `libacoustic` to `libaudition`.
- Renamed the public C++ namespace and exported CMake target namespace from `acoustic` to `audition`.
- Moved installed headers from `include/acoustic/` to `include/audition/` and renamed the umbrella header to `audition/audition.hpp`.
- Renamed build options and package configuration variables from `LIBACOUSTIC_*` to `LIBAUDITION_*`.

## [0.1.0] - 2026-10-07

### Added

- Apache-2.0 project foundation and permissive third-party/model policy.
- C++17 core domain model for audio, clocks, geometry, uncertainty, IDs and events.
- Explicit `SpatialTrackId`, generic persistent `AcousticSourceId`, and `SpeakerId` semantics.
- Narrow SOLID interfaces for audio I/O, AEC, suppression, resampling, spatial processing,
  range/fusion, VAD, speech segmentation, offline/streaming ASR, KWS, language ID,
  classification, embeddings, speaker analysis, voice traits/state, authenticity and TTS.
- Capability metadata and hardware-neutral `ExecutionTarget`.
- Typed factory registry for explicitly linked replaceable backends.
- Model descriptor and in-memory model registry with licensing/provenance fields.
- Generic source, speaker and sound-prototype repository contracts with in-memory implementations.
- Bounded backpressure queues and cancellation primitives without imposing a scheduler.
- Pluggable logging contract and optional synchronous spdlog adapter.
- Dependency-free DSP primitives: RMS, per-channel RMS, peak, clipping ratio, layout conversion,
  and mono mixdown.
- CMake install/export package targets and presets.
- GoogleTest unit tests and public API examples.
- Doxygen configuration, architecture specification, ADRs and Mermaid diagram sources.
- GitHub Actions build/test workflow.

### Validated

- GCC 14.2 strict build with warnings-as-errors for the dependency-free configuration.
- Clang 17 strict build with warnings-as-errors for the dependency-free configuration.
- Example executables run successfully.
- Installed CMake package consumed successfully by a separate downstream project using
  `find_package(libaudition CONFIG REQUIRED)`.

### Not included yet

ODAS, sherpa-onnx, CLAP, AASIST, WORLD, WebRTC AEC3 and GTSAM adapters are intentionally
scheduled for subsequent releases so their concrete integration does not distort the core API.
