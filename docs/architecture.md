# Architecture

The dependency rule is: **core owns abstractions, backends implement them,
pipelines depend on abstractions, applications compose blocks**.

```mermaid
flowchart TB
    APP[Application / ROS2 / desktop / embedded]

    subgraph LIB[libaudition]
      subgraph PIPE[Composition and infrastructure]
        QUEUE[BoundedQueue]
        REG[FactoryRegistry]
        MODEL[ModelRegistry]
      end

      subgraph PORTS[Algorithm interfaces]
        AUDIO[IAudioSource / IEchoCanceller / INoiseSuppressor]
        SPATIAL[ISpatialAudioEngine / IRangeEstimator / ISpatialFusion]
        SPEECH[IVAD / IAsr / IKWS / ILanguageIdentifier]
        SEM[IAudioClassifier / IAudioEmbedder]
        SPEAKER[ISpeakerEmbedder / ISpeakerVerifier / ISpeakerIdentifier]
        VOICE[IVoiceTraitsEstimator / IVoiceStateEstimator]
        AUTH[IAudioAuthenticityDetector]
        TTS[ISpeechSynthesizer]
        ID[ISourceIdentityResolver]
      end

      subgraph CORE[Stable domain]
        BUF[AudioBuffer / AudioView]
        TRACK[SpatialTrack / TrackedAudioFrame]
        SEG[SpeechSegment / Transcript]
        OBS[Classification / Embeddings / VoiceTraits]
        GEO[Direction / Range / Position + covariance]
        EVENT[AcousticEvent]
      end
    end

    subgraph BACKENDS[Replaceable adapters]
      ODAS[ODAS]
      SHERPA[sherpa-onnx]
      CLAP[CLAP]
      AASIST[AASIST]
      WORLD[WORLD]
      GTSAM[GTSAM]
      AEC[WebRTC AEC3]
      CUSTOM[Application/custom backend]
    end

    APP --> PIPE
    APP --> PORTS
    PIPE --> PORTS
    PORTS --> CORE

    ODAS -. implements .-> SPATIAL
    SHERPA -. implements .-> SPEECH
    SHERPA -. implements .-> SPEAKER
    SHERPA -. implements .-> TTS
    CLAP -. implements .-> SEM
    AASIST -. implements .-> AUTH
    WORLD -. implements .-> VOICE
    GTSAM -. implements .-> SPATIAL
    AEC -. implements .-> AUDIO
    CUSTOM -. implements .-> PORTS
```

Authoritative Mermaid source: `docs/images/architecture.mmd`.

## Source identity

```mermaid
flowchart LR
    T1[SpatialTrack 12] --> R[ISourceIdentityResolver]
    T2[SpatialTrack 37] --> R
    F1[Fingerprint] --> R
    G[Geometry + time] --> R
    R --> S[AcousticSourceId 5]
    S --> H[Persistent history]
    SP[Speaker embedding] --> P[SpeakerId Ivan]
    P -. may annotate .-> S
```

A persistent acoustic source is generic; speaker identity is optional evidence,
not the definition of a source.

## Application-controlled scheduling

```mermaid
flowchart LR
    CAP[Capture block] --> Q1[Bounded queue]
    Q1 --> ODAS[Spatial block]
    ODAS --> Q2[Per-track queue]
    Q2 --> VAD[VAD block]
    Q2 --> CLS[Classifier block]
    VAD --> SEG[Segmenter]
    SEG --> ASR[ASR block]
```

The diagram is a possible application graph, not an internal mandatory thread
layout. The same blocks may be called synchronously in an offline program.
