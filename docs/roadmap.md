# Roadmap

## v0.1 — foundation

- Stable vocabulary and ownership/time/coordinate contracts.
- Replaceable interfaces for spatial, speech, classification, speaker, voice, authenticity,
  synthesis, fusion and resampling algorithms.
- Persistent source/speaker/sound-prototype repository contracts.
- Factory registry, model registry, bounded queue, cancellation and logging infrastructure.
- Basic dependency-free DSP operations.
- CMake install/export package, GoogleTest suite, Doxygen, ADRs and CI.

## v0.2 — spatial backend

- Direct ODAS adapter (no `odas_ros`).
- Microphone-array geometry/calibration types.
- Stable mapping between separated stream slots and spatial track IDs.
- Typed ODAS configuration, capability reporting, reset semantics and contract tests.
- Recorded real-scene fixtures remain a follow-up once representative hardware captures are curated.

## v0.3 — sherpa-onnx backend family

- VAD, offline/streaming ASR, KWS, language ID.
- Speaker embedding/verification/diarization.
- Audio tagging, enhancement, TTS and permitted voice-cloning models.

## v0.4 — extended perception

- CLAP embeddings/open-vocabulary classification and sound prototype matching.
- AASIST authenticity backend.
- WORLD voice feature backend.
- WebRTC AEC3 adapter.

## v0.5 — spatial inference and fusion

- GTSAM bearing/range/position fusion.
- SPL/type range priors.
- Persistent acoustic-source identity resolver.
- Multimodal observation ports suitable for vision/radar integration.

## 1.0

- Stabilize API/ABI policy after real ROS 2 and standalone deployments.
- Consider a versioned DSO plugin ABI only after interface semantics are stable.
