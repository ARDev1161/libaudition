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

Speech-enhancement slice:
- GTCRN and DPDFNet through `INoiseSuppressor`.
- Explicit offline versus streaming semantics.
- Streaming `flush()`, model-derived preferred frame size and no hidden resampling.

TTS slice:
- Optional Sherpa TTS build support.
- VITS/Piper, Matcha and Kokoro through `ISpeechSynthesizer`.
- Request-time speed, configured speaker ID and explicit language declaration.
- Voice references are rejected rather than ignored.

Follow-up slices:
- ZipVoice/Pocket and other reference-audio/voice-cloning models with explicit core semantics.

## v0.4 — extended perception

Semantic embedding foundation:
- Optional embedding quality rather than fabricated zero confidence.
- Audio-embedding and open-vocabulary capability reporting.
- Backend-neutral cosine sound-prototype matching over persistent few-shot memory.
- Strict `model_id`/dimension isolation between embedding spaces.

CLAP audio-embedding slice:
- Standalone ONNX Runtime adapter through `IAudioEmbedder`.
- Strict mono 48 kHz input with no hidden resampling or downmixing.
- Deterministic HTSAT frontend with model-defined repeat-padding for short clips and explicit segmentation for audio longer than 10 seconds.
- 512-dimensional embedding-space isolation through explicit `model_id`.

CLAP open-vocabulary slice:
- Private RoBERTa byte-level BPE tokenizer loaded from the model's pinned `tokenizer.json`.
- ONNX text encoder producing embeddings in the same 512-dimensional CLAP space.
- `IOpenVocabularyAudioClassifier` using cosine similarity and explicit candidate-relative softmax temperature.
- Candidate-relative values are normalized probabilities over the supplied label set, not calibrated real-world event probabilities.

Follow-up slices:
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
