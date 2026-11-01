# libaudition

**Composable C++17 acoustic perception toolkit** for spatial audio, source tracking, speech, sound classification, acoustic identity, and temporal event analysis.

The library provides reusable algorithms and acoustic observations. Applications own capture, scheduling, storage, ROS 2 integration, multimodal fusion, and actions. Optional ML backends and model weights are separate from the dependency-light core.

## Modular architecture

This repository-oriented diagram is adapted from the [GitDiagram overview](https://gitdiagram.com/ardev1161/libaudition), expanded to include the optional spatial, speech, audio, speaker and semantic backends as well as the identity and temporal layers. **Solid arrows** illustrate possible data flow; **dashed arrows** show optional compositions or replaceable adapters. It is **not** a fixed processing pipeline or a declaration that every backend is installed.

Click a linked node to inspect its implementation on GitHub. Click the diagram background to open the zoomable viewer. Applications still own capture, worker scheduling, recordings and robot/ROS 2 behavior; a mono-only application can bypass ODAS.

```mermaid
flowchart TB
  %% A curated, repository-grounded composition map, not a fixed runtime.
  APP(["Consumer application<br/>capture / scheduling / policy"])

  subgraph AUDIO["Audio / DSP and reusable contracts"]
    direction LR
    BUF["AudioBuffer / AudioView<br/>core"]
    IO["WAV I/O / timeline<br/>io"]
    FRONT["Audio frontend<br/>dsp"]
    PORTS["Backend-neutral interfaces"]
    ENHANCE["Resampling / WebRTC APM<br/>optional"]
  end

  subgraph SPATIAL["Spatial perception"]
    direction LR
    ODAS["ODAS engine<br/>DOA / tracking / separation"]
    TRACK["SpatialTrack + separated PCM"]
    RANGE["SPL / range prior / smoothing"]
    FUSION["GTSAM spatial fusion<br/>optional"]
  end

  subgraph SPEECH["Per-source speech and voice"]
    direction LR
    INGRESS["Async speech ingress"]
    SPEECH_RT["PerSourceSpeechRuntime"]
    VAD["VAD: Silero / TEN<br/>sherpa-onnx"]
    ASR["ASR: sherpa-onnx / whisper.cpp"]
    SPKR["Speaker embedding / identity<br/>sherpa-onnx"]
    VOICE["WORLD voice traits<br/>optional"]
    TTS["TTS via sherpa-onnx<br/>optional output"]
  end

  subgraph SEMANTICS["Sound semantics"]
    direction LR
    CLASS_RT["Source classification runtime"]
    TAG["YAMNet / EfficientAT / sherpa<br/>optional sound taggers"]
    CLAP["CLAP audio embeddings"]
    AUTH["AASIST authenticity<br/>optional"]
  end

  subgraph MEMORY["Identity and acoustic memory"]
    direction LR
    ID_COORD["SpatialIdentityCoordinator"]
    RESOLVER["Heuristic source resolver"]
    PROTOTYPE["Sound prototype matching"]
  end

  subgraph EVENTS["Temporal analysis and events"]
    direction LR
    DETECT["Temporal event detector"]
    PATTERN["Recurrence / DTW / patterns"]
    ASSEMBLER["AcousticEventAssembler"]
  end

  RESULT["Acoustic observations / events<br/>returned to application"]

  APP -->|"supplies PCM"| BUF
  APP -.->|"uses when needed"| IO
  BUF --> FRONT
  BUF -->|"calibrated array channels"| ODAS
  FRONT -.->|"optional frontend"| ENHANCE
  FRONT -->|"mono / non-spatial path"| INGRESS
  ODAS -->|"tracks and separated audio"| TRACK
  TRACK -->|"per-track PCM"| INGRESS
  TRACK -->|"per-track PCM"| CLASS_RT
  TRACK -->|"track evidence"| ID_COORD
  RANGE -.-> ID_COORD
  FUSION -.->|"optional fusion"| ID_COORD
  ID_COORD --> RESOLVER
  INGRESS --> SPEECH_RT
  VAD -.->|"selected engine"| SPEECH_RT
  ASR -.->|"selected engine"| SPEECH_RT
  SPKR -.->|"optional voice evidence"| RESOLVER
  FRONT -.->|"voice analysis"| VOICE
  VOICE -.->|"voice measurements"| RESULT
  CLASS_RT -->|"scores"| DETECT
  TAG -.->|"selected classifier"| CLASS_RT
  CLAP -.->|"embeddings / labels"| CLASS_RT
  BUF -.->|"authenticity analysis"| AUTH
  AUTH -.->|"spoof evidence"| RESULT
  RESOLVER -->|"AcousticSourceId"| ASSEMBLER
  PROTOTYPE -.->|"optional sound fingerprints"| RESOLVER
  SPEECH_RT -->|"transcripts / segments"| ASSEMBLER
  DETECT -->|"start / end events"| ASSEMBLER
  PATTERN -.->|"temporal evidence"| ASSEMBLER
  ASSEMBLER --> RESULT
  %% The application consumes RESULT; avoid a graph cycle back to APP.
  APP -.->|"explicit synthesis"| TTS
  PORTS -.->|"implemented by adapters"| TAG
  PORTS -.->|"implemented by adapters"| VAD
  PORTS -.->|"implemented by adapters"| ODAS

  %% Click nodes for actual source locations; names are from verified paths.
  click BUF "https://github.com/ARDev1161/libaudition/blob/main/include/audition/audio/audio_buffer.hpp" "Public audio buffer" _blank
  click IO "https://github.com/ARDev1161/libaudition/blob/main/src/io/wav_io.cpp" "WAV I/O" _blank
  click FRONT "https://github.com/ARDev1161/libaudition/blob/main/src/dsp/audio_frontend.cpp" "Audio frontend" _blank
  click PORTS "https://github.com/ARDev1161/libaudition/tree/main/include/audition" "Public contracts" _blank
  click ENHANCE "https://github.com/ARDev1161/libaudition/blob/main/backends/webrtc_aec3/src/webrtc_apm_echo_canceller.cpp" "WebRTC APM adapter" _blank
  click ODAS "https://github.com/ARDev1161/libaudition/blob/main/backends/odas/src/odas_spatial_engine.cpp" "ODAS adapter" _blank
  click TRACK "https://github.com/ARDev1161/libaudition/blob/main/backends/odas/src/detail/odas_runtime.hpp" "ODAS runtime" _blank
  click RANGE "https://github.com/ARDev1161/libaudition/blob/main/src/spatial/level_range_prior.cpp" "Range prior" _blank
  click FUSION "https://github.com/ARDev1161/libaudition/blob/main/backends/gtsam/src/gtsam_spatial_fusion.cpp" "GTSAM spatial fusion" _blank
  click INGRESS "https://github.com/ARDev1161/libaudition/blob/main/include/audition/speech/async_speech_ingress_worker.hpp" "Async speech ingress" _blank
  click SPEECH_RT "https://github.com/ARDev1161/libaudition/blob/main/src/pipeline/per_source_speech_runtime.cpp" "Per-source speech runtime" _blank
  click VAD "https://github.com/ARDev1161/libaudition/blob/main/backends/sherpa/src/sherpa_vad.cpp" "Sherpa VAD" _blank
  click ASR "https://github.com/ARDev1161/libaudition/blob/main/backends/whispercpp/src/whispercpp_asr.cpp" "whisper.cpp ASR adapter" _blank
  click SPKR "https://github.com/ARDev1161/libaudition/blob/main/backends/sherpa/src/sherpa_speaker_embedder.cpp" "Speaker embedding" _blank
  click VOICE "https://github.com/ARDev1161/libaudition/blob/main/backends/world/src/world_voice_traits_estimator.cpp" "WORLD voice traits" _blank
  click TTS "https://github.com/ARDev1161/libaudition/blob/main/backends/sherpa/src/sherpa_tts.cpp" "Optional sherpa TTS" _blank
  click CLASS_RT "https://github.com/ARDev1161/libaudition/blob/main/src/pipeline/source_classification_runtime.cpp" "Classification runtime" _blank
  click TAG "https://github.com/ARDev1161/libaudition/blob/main/backends/yamnet/src/yamnet_audio_tagger.cpp" "YAMNet adapter" _blank
  click CLAP "https://github.com/ARDev1161/libaudition/blob/main/backends/clap/src/clap_audio_embedder.cpp" "CLAP adapter" _blank
  click AUTH "https://github.com/ARDev1161/libaudition/blob/main/backends/aasist/src/aasist_authenticity_detector.cpp" "AASIST authenticity" _blank
  click ID_COORD "https://github.com/ARDev1161/libaudition/blob/main/src/pipeline/spatial_identity.cpp" "Spatial identity coordinator" _blank
  click RESOLVER "https://github.com/ARDev1161/libaudition/blob/main/src/memory/source_identity_resolver.cpp" "Persistent source resolver" _blank
  click PROTOTYPE "https://github.com/ARDev1161/libaudition/blob/main/src/memory/sound_prototype_matcher.cpp" "Sound prototype matcher" _blank
  click DETECT "https://github.com/ARDev1161/libaudition/blob/main/src/pipeline/temporal_acoustic_event_detector.cpp" "Temporal event detector" _blank
  click PATTERN "https://github.com/ARDev1161/libaudition/blob/main/include/audition/temporal/streaming_pattern_matcher.hpp" "Temporal pattern matching" _blank
  click ASSEMBLER "https://github.com/ARDev1161/libaudition/blob/main/src/pipeline/acoustic_event_assembler.cpp" "Acoustic event assembler" _blank

  classDef toneBlue fill:#dbeafe,stroke:#2563eb,color:#172554
  classDef toneAmber fill:#fef3c7,stroke:#d97706,color:#78350f
  classDef toneMint fill:#dcfce7,stroke:#16a34a,color:#14532d
  classDef toneRose fill:#ffe4e6,stroke:#e11d48,color:#881337
  classDef toneIndigo fill:#e0e7ff,stroke:#4f46e5,color:#312e81
  classDef toneTeal fill:#ccfbf1,stroke:#0f766e,color:#134e4a
  classDef toneNeutral fill:#f8fafc,stroke:#334155,color:#0f172a

  class BUF,IO,FRONT,PORTS,ENHANCE toneBlue
  class ODAS,TRACK,RANGE,FUSION toneAmber
  class INGRESS,SPEECH_RT,VAD,ASR,SPKR,VOICE,TTS toneMint
  class CLASS_RT,TAG,CLAP,AUTH toneTeal
  class ID_COORD,RESOLVER,PROTOTYPE toneIndigo
  class DETECT,PATTERN,ASSEMBLER toneRose
  class APP,RESULT toneNeutral
```

**Source identity is separate from speaker identity:** an ODAS `SpatialTrackId` is temporary, a resolved `AcousticSourceId` is a persistent acoustic hypothesis, and `SpeakerId` describes a person speaking. The `SpatialIdentityCoordinator` composes spatial evidence and a resolver without forcing a particular fusion backend. Denoising/AEC belongs to a speech-oriented branch, not ahead of multichannel localization by default.

For the detailed algorithm inventory and alternate compositions, see the [functional architecture and algorithm map](functional_architecture.md).

### Main modules

These are the actual CMake-exported building blocks. Their targets have the form `audition::<module>`.

- **[core](specification.md)** — common C++ types, PCM buffers and views, timestamps, clock identities and error handling; the foundation of the SDK.
- **[interfaces](backend_contracts.md)** — abstract contracts for spatial analysis, VAD/ASR, sound classification, speaker analysis and other interchangeable engines.
- **[io](cli_examples.md)** — reusable WAV file I/O and recording-timeline serialization, independent of Qt or audio-device capture.
- **[dsp](audio_frontend.md)** — basic signal processing, channel routing/calibration, level and quality measurements, and audio-front-end utilities.
- **[spatial](functional_architecture.md)** — acoustic geometry, calibrated SPL/range priors and temporal smoothing; separation/localization engines are provided by optional adapters.
- **[pipeline](per_source_speech.md)** — bounded composition/runtime helpers: source-aware classification, per-source speech, temporal events, source tracking/identity handoff and event assembly.
- **[memory](spatial_identity_integration.md)** — registries for acoustic sources, speakers and sound prototypes, with matching and persistent source-identity resolution.
- **[model](models.md)** — a backend-neutral in-memory registry for model metadata; model weights and runtime engines remain separate.
- **[logging](api.rst)** — logging abstractions and integration points without committing applications to a particular logging implementation.

### Optional backends

The adapters implement selected interfaces and are enabled independently at build time. They are **not all installed by default**; ML model weights are also separate.

- **Spatial:** [ODAS](backends/odas.md) estimates source directions, tracks sources and separates audio; [GTSAM](backends/gtsam.md) provides optional acoustic-only geometric fusion.
- **Audio enhancement:** [libsamplerate](audio_frontend.md) performs stateful resampling; [WebRTC APM](audio_frontend.md) offers echo cancellation and gain processing; [SpeexDSP](speech-agc-backends.md) is an optional source-gain processor.
- **Speech and synthesis:** [sherpa-onnx](backends/sherpa.md) supplies VAD, speech recognition, keyword spotting, speaker and TTS capabilities according to the chosen models; [whisper.cpp](ACOUSTIC_SCENE_SPEECH_BACKENDS.md) is an alternative ASR engine.
- **Sound classification and embeddings:** [YAMNet](backends/yamnet.md) tags AudioSet events; [EfficientAT](backends/efficientat.md) provides experimental audio tagging; [CLAP](backends/clap.md) provides semantic audio/text embeddings.
- **Voice characterization:** [WORLD](backends/world.md) extracts acoustic voice features; [AASIST](backends/aasist.md) detects audio spoofing with its optional model.

**Application boundary:** microphone capture, scheduling policy, user interfaces, ROS 2, camera/radar fusion and robot navigation belong to consuming applications, not the libaudition core. For detailed signal-flow and algorithm diagrams, see the [functional architecture](functional_architecture.md).

## Documentation
- [Source code](https://github.com/ARDev1161/libaudition)
- [Current release-candidate status](release_v1_rc1.md)
- [Functional architecture and algorithm map](functional_architecture.md)
- [C++ API reference in Sphinx](api.rst)
- [Full Doxygen HTML reference](https://ardev1161.github.io/libaudition/doxygen/index.html) — searchable classes, namespaces and public headers

~~~{toctree}
:caption: Getting started
:maxdepth: 2

getting_started
cli_examples
full_demo_build
qt_demo
~~~

~~~{toctree}
:caption: Architecture and contracts
:maxdepth: 2

functional_architecture
architecture
specification
backend_contracts
audio_frontend
per_source_speech
temporal_acoustic_events
temporal_patterns
~~~

~~~{toctree}
:caption: Optional backends
:maxdepth: 1

backends/odas
backends/sherpa
backends/yamnet
backends/efficientat
backends/clap
backends/aasist
backends/world
backends/gtsam
~~~

~~~{toctree}
:caption: C++ reference
:maxdepth: 2

api
Doxygen HTML API <https://ardev1161.github.io/libaudition/doxygen/index.html>
~~~

~~~{toctree}
:caption: Development and releases
:maxdepth: 1

development
development_roadmap
models
test_maintenance_map
ctest_suites
algorithm_test_matrix
qt_model_validation_matrix
release_v1_rc1
release_v1_acceptance
site_build
~~~

The website is built from the source documentation in this repository. API declarations are generated from public headers in the same commit; check the release notes and backend requirements before assuming an optional model or device has been validated.
