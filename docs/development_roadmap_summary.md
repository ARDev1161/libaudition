# libaudition — Compact Development Roadmap

This is a condensed companion to the [full technical roadmap](development_roadmap.md), not a replacement.

Status: proposed; not a release commitment. This roadmap is scoped to the reusable acoustic-processing library. ROS 2 integration, navigation, visual/radar fusion and application policy remain outside the library.

## Current baseline

The repository already includes backend adapters for ODAS, sherpa-onnx, WebRTC AEC3, YAMNet, EfficientAT, CLAP, WORLD, AASIST and GTSAM, together with source identity, recording and temporal-event components. Presence of an adapter or passing synthetic tests does not establish real-world accuracy.

## Priorities

1. **P0: DSP primitives.** Publish reusable FFT/STFT, filtering, spectral-feature and streaming contracts rather than leaving these operations embedded inside model-specific frontends.
2. **P0: Recorded-data validation.** Build versioned reference datasets, annotation rules, reproducible benchmark runners and failure-mode reports.
3. **P0: Source lifecycle.** Resolve onset sensitivity and stale-track behavior using recorded microphone-array cases and explicit source expiry policies.
4. **P1: Spatial alternatives.** Add independent DOA and beamforming implementations, with comparisons against ODAS.
5. **P1: Event-level analysis.** Extend source-bound classification, recurring-pattern recognition, open-set detection and anomaly evaluation.
6. **P1/P2: Packaging and performance.** Benchmark x86/ARM64, improve documentation and packaging, and add optional hardware acceleration or language bindings based on measured demand.

## Phase acceptance

Each implementation must include a documented API, bounded-state behavior where streaming is supported, deterministic numerical tests, independently annotated real-audio evaluation when quality is claimed, and resource measurements on relevant targets. A CI pass is not equivalent to a quality benchmark. Missing corpora must be reported as not evaluated, not passed.

## Diagram

The source diagram is [development_roadmap_summary.mmd](development_roadmap_summary.mmd). GitHub Markdown renders the same diagram below.

```mermaid
flowchart TB
  A["Baseline: v1.0 RC\nExisting backend adapters, temporal analysis, identity and CI"]
  subgraph F["Phase 1 — Core DSP and API"]
    F1["FFT / inverse FFT; STFT / ISTFT; window functions"]
    F2["FIR / IIR; biquad; overlap-add; resampling contracts"]
    F3["Spectral features: mel, MFCC, energy, flux, centroid, pitch"]
    F4["Streaming APIs; sample clocks; allocation budgets"]
    F1 --> F2 --> F3 --> F4
  end
  subgraph V["Phase 2 — Validation and measurement"]
    V1["Reference corpora and dataset manifests"]
    V2["Numerical oracles; regression tests; differential comparisons"]
    V3["WER / DER / DOA error / event F1 / source ID switches"]
    V4["Latency p50/p95/p99; memory; CPU; ARM64 measurements"]
    V1 --> V2 --> V3 --> V4
  end
  subgraph S["Phase 3 — Streaming source reliability"]
    S1["Adaptive noise floor and calibrated signal-level estimation"]
    S2["Source onset/offset hysteresis; timeout and lifecycle policy"]
    S3["Source-local VAD/ASR; gaps, overload, retirement, ID reuse"]
    S4["ReSpeaker multichannel recordings and physical acceptance"]
    S1 --> S2 --> S3 --> S4
  end
  subgraph P["Phase 4 — Spatial algorithm coverage"]
    P1["GCC-PHAT / SRP-PHAT / MUSIC reference implementations"]
    P2["Delay-and-sum / MVDR beamforming"]
    P3["DOA uncertainty, array calibration, tracking and reacquisition"]
    P4["Comparison against ODAS on labeled multichannel recordings"]
    P1 --> P2 --> P3 --> P4
  end
  subgraph M["Phase 5 — Acoustic event analysis"]
    M1["Classifier/embedding calibration and open-set detection"]
    M2["Event segmentation, temporal association, recurring patterns"]
    M3["Source-local prototype storage and bounded retrieval"]
    M4["Anomaly detection with measurable false alarm rates"]
    M1 --> M2 --> M3 --> M4
  end
  subgraph E["Phase 6 — Portability and developer integration"]
    E1["Stable C++17 API, package exports, examples, documentation"]
    E2["Optional SIMD/NEON and backend acceleration"]
    E3["RK3588 latency, memory and thermal profiling"]
    E4["Optional Python bindings and release artifacts"]
    E1 --> E2 --> E3 --> E4
  end
  A --> F1
  A --> V1
  A --> S1
  F4 --> P1
  S4 --> P4
  V3 --> P4
  V3 --> M1
  P4 --> M2
  M4 --> E1
  V4 --> E3
  subgraph G["Acceptance criteria (apply to each phase)"]
    G1["Public API and usage example"]
    G2["Deterministic tests and independent reference"]
    G3["Representative recorded-audio evaluation"]
    G4["Performance and failure-mode report"]
  end
  E4 --> G1 --> G2 --> G3 --> G4
```
