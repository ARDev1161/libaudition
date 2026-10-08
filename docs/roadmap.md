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

## v0.2.x — audio frontend

- Explicit channel routing and microphone gain/DC/fractional-delay calibration.
- Measured signal-quality metrics.
- Streaming libsamplerate backend.
- WebRTC AEC3 adapter for the speech branch with explicit playback reference.
- Keep raw/calibrated microphone-array audio available to spatial processing.

## v0.3 — sherpa-onnx backend family

First speech slice:
- Silero/TEN VAD.
- Offline ASR: transducer, Paraformer, NeMo CTC, Whisper, SenseVoice, Zipformer CTC and WeNet CTC.
- Streaming ASR: transducer, Paraformer, Zipformer2 CTC, NeMo CTC and T-One CTC.
- Keyword spotting.
- Whisper spoken-language identification.
- Hardware-neutral provider pass-through through `ExecutionTarget`.

Speaker slice:
- Speaker embedding extraction.
- Verification and transient identification search index.
- Offline diarization with local cluster labels.
- Persistent speaker memory remains a separate library layer.

Audio-tagging slice:
- Zipformer and CED audio tagging through `IAudioClassifier`.
- Explicit labels, top-K selection and mono 16 kHz contract without hidden resampling.

Follow-up slices:
- Speech enhancement with explicit streaming flush/latency semantics.
- TTS and permitted voice-cloning models.

## v0.4 — extended perception

- CLAP embeddings/open-vocabulary classification and sound prototype matching.
- AASIST authenticity backend.
- WORLD voice feature backend.

## v0.5 — spatial inference and fusion

- GTSAM bearing/range/position fusion.
- SPL/type range priors.
- Persistent acoustic-source identity resolver.
- Multimodal observation ports suitable for vision/radar integration.

## 1.0

- Stabilize API/ABI policy after real ROS 2 and standalone deployments.
- Consider a versioned DSO plugin ABI only after interface semantics are stable.
