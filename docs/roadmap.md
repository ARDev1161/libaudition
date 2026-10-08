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

AASIST authenticity slice:
- Official AASIST ONNX waveform model behind `IAudioAuthenticityDetector`.
- Explicit mono 16 kHz / 64600-frame analysis contract with model-defined repeat-padding for shorter clips.
- Raw bona-fide/spoof logits represented as `Score`, never fabricated as calibrated probabilities.
- Optional externally fitted Platt calibration can populate bona-fide/spoof `Probability`.
- No replay-vs-synthetic attribution is claimed by this binary detector.

WORLD voice-feature slice:
- Pinned modified-BSD WORLD backend through `IVoiceTraitsEstimator`.
- DIO + StoneMask and Harvest F0 estimation with explicit typed configuration.
- Arithmetic mean and population standard deviation over voiced F0 frames.
- Unvoiced/insufficient speech yields absent pitch fields rather than fabricated zero.
- Age, categorical voice traits and speaking rate remain unsupported/absent.

WORLD acoustic-feature slice:
- Backend-neutral `IVoiceAcousticAnalyzer` for frame-level voice measurements.
- F0 contour and time axis from DIO + StoneMask or Harvest.
- CheapTrick spectral envelope and D4C aperiodicity in row-major frame/bin matrices.
- Explicit frame period, FFT size and frequency-bin dimensions without leaking WORLD-native types.

Follow-up slices:
- GTSAM spatial inference and acoustic-source fusion.

## v0.5 — spatial inference and fusion

GTSAM foundation:
- Backend-neutral bearing/range/position observations remain in core interfaces.
- Range observations are anchored to an explicit sensor pose.
- Optional GTSAM 4.2 backend performs batch bearing/range/position fusion and returns marginal covariance.
- Mixed clock identities are rejected before geometric fusion.

FUS-03 robustness:
- Multi-bearing batches use deterministic least-squares ray intersection for initialization.
- Bearing-only parallel/rank-deficient geometry is rejected instead of producing a fabricated point.
- Range-only 3D fusion requires non-coplanar sensor geometry.
- Input position covariance is required to be positive semidefinite and output marginal covariance is sanity-checked.
- Optional Huber loss can robustify bearing/range/position source measurements without weakening fixed sensor-pose priors.

ID-01 persistent acoustic-source identity:
- Active `SpatialTrackId` continuity is authoritative until explicit `endTrack()`.
- Ended tracks can reacquire a persistent `AcousticSourceId` using compatible acoustic fingerprints and/or recent geometry.
- Fingerprint spaces are isolated by `model_id` and embedding dimension.
- One persistent source cannot be assigned to two simultaneous active tracks.
- Resolver state enforces one clock identity and globally non-decreasing observation timestamps.
- `reset()` clears transient track/geometry state while persistent registry fingerprints remain available for re-identification.

ID-02 spatial/identity integration:
- Backend-independent `SpatialIdentityCoordinator` composes `ISpatialFusion` with `ISourceIdentityResolver`.
- Fused position is supplied to source identity before committing the updated track.
- Fusion observations must share the track clock identity and must not be newer than `track.last_seen`.
- Persistent `AcousticSourceId` propagates to `SpatialTrack`, matching `TrackedAudioFrame`, and `AcousticEvent`.
- Batch propagation validates all conflicts before mutation, avoiding partial updates.
- Track end/reset lifecycle is forwarded to the resolver and clears active coordinator bindings.

Next slices:
- SPL/type range priors.
- Multimodal observation ports suitable for vision/radar integration.

## 1.0

- Stabilize API/ABI policy after real ROS 2 and standalone deployments.
- Consider a versioned DSO plugin ABI only after interface semantics are stable.
