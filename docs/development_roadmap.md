# libaudition — development roadmap

> Status: proposed technical plan (updated 8 November 2026). This document describes intended work, not a delivery commitment or a claim that all listed features are implemented.

## Scope and baseline

`libaudition` provides application-independent acoustic processing and observations with a C++17 public API. Existing components include audio representations and frontend utilities, ODAS spatial integration, GTSAM spatial fusion, speech and speaker adapters, YAMNet/EfficientAT/CLAP classification, temporal event/pattern algorithms and bounded per-source processing.

The v1.0 release candidate is not equivalent to a published stable release. The development plan must distinguish **present implementation**, **proposed work**, and **measured validation**. A passing unit test does not establish model accuracy or hardware real-time behavior.

The existing implementation history and released-version planning remain in [roadmap.md](roadmap.md). This document covers proposed work beyond the present release candidate.

## Long-term vision (proposed post-v1.0)

**libaudition aims to be a hardware-agnostic, application-independent C++17 toolkit for machine hearing**, from reusable DSP through spatial/speech/semantic perception to time-aware acoustic scenes, optional memory and attention-oriented analysis. It supports three composable usage tiers:

1. **DSP toolkit** — buffers, clocks, calibrated filtering and array transforms; no ML dependency.
2. **Acoustic perception** — source localization, tracking, separation, speech, speaker identity, sound categories and timed events.
3. **Cognitive audition** — an acoustic scene with uncertainty, evidence provenance, optional memory, novelty and suggestions for deeper analysis.

All tiers are **audio-only**. Robot actions, policy, ROS 2, camera/radar fusion, task relevance enforcement, scheduling and data-retention decisions remain application responsibilities. Maintain the lightweight default build, replaceable optional backends, C++17 public API, bounded state and explicit time/source identities. No future research milestone is a prerequisite for the existing v1.0 release candidate. Prefer integrating proven backends to reimplementing entire research engines without demonstrated benefit.

**Claims of parity or superiority over HARK or ODAS require evidence** from the same multichannel recordings, geometry, calibration, downstream ASR and held-out scoring; a 16-mic research demonstration cannot be treated as a four-mic performance requirement.

## Diagram

[View the SVG overview](images/development_roadmap_overview.svg) · [Mermaid source](images/development_roadmap.mmd)

The overview is a compact, static summary. The Mermaid diagram below contains all work items and their dependencies and is rendered directly by GitHub.

```mermaid
flowchart TB
%% Proposed libaudition development roadmap. Scope is acoustic-only; versions are not delivery promises.
ROOT["libaudition development roadmap<br/>Proposed technical milestones; no committed dates"]
subgraph M0["M0 — Release baseline / P0"]
direction TB
 R01["R01 — Verify the exact release commit against the full CI matrix (Linux, macOS, sanitizers, optional backends)."]
 R02["R02 — Freeze and document public C++17 contracts: AudioView, timestamps, clock domains, source and speaker IDs, uncertainty."]
 R03["R03 — Validate CMake install/export, static/shared consumers, third-party notices and model provenance."]
 R04["R04 — Document unsupported deployment claims and decide whether the release candidate can be tagged."]
 M0G{{"M0 gate — Exact-commit CI and independent installed consumer pass; exclusions and licenses recorded."}}
 R01 --> M0G
 R02 --> M0G
 R03 --> M0G
 R04 --> M0G
end
subgraph M1["M1 — Reusable signal processing / P0"]
direction TB
 D01["D01 — Provide FFT/IFFT plans, real/complex transforms, frequency-bin conventions and documented scaling."]
 D02["D02 — Add Hann/Hamming/Blackman windows and streaming STFT/ISTFT with defined reconstruction and latency."]
 D03["D03 — Provide FIR/IIR/biquad filters, high-order fractional delay and explicit reset/flush behavior."]
 D04["D04 — Expose spectral features: PSD, log-mel, MFCC, spectral flux, onset, pitch and harmonicity."]
 D05["D05 — Add calibrated A/C weighting, noise-floor estimation and separately configurable AGC, gate and limiter."]
 M1G{{"M1 gate — Independent numerical references, reconstruction tolerances, chunk/reset tests and memory bounds."}}
 D01 --> M1G
 D02 --> M1G
 D03 --> M1G
 D04 --> M1G
 D05 --> M1G
end
subgraph M2["M2 — Streaming and source lifecycle / P0"]
direction TB
 T01["T01 — Instrument per-channel levels, clipping, dropout, timestamp drift and capture-to-inference delays."]
 T02["T02 — Introduce adaptive noise-floor estimation, onset thresholds, hysteresis, debounce and expiry tuning."]
 T03["T03 — Specify source states: tentative, confirmed, inactive, lost and retired; prevent stale spatial tracks."]
 T04["T04 — Harden per-source PCM epochs, backpressure, source retirement, late ASR results and ID reuse."]
 T05["T05 — Keep spatial PCM phase-consistent; apply AEC/noise reduction/AGC on an explicit speech branch."]
 M2G{{"M2 gate — Quiet/short-source corpus regressions; no stale tracks or source contamination; bounded queues."}}
 T01 --> M2G
 T02 --> M2G
 T03 --> M2G
 T04 --> M2G
 T05 --> M2G
end
subgraph M3["M3 — Spatial algorithms and tracking / P1"]
direction TB
 S01["S01 — Version microphone-array geometry and measured gain, phase and delay calibration profiles."]
 S02["S02 — Implement pluggable GCC-PHAT, SRP-PHAT and MUSIC localization baselines alongside ODAS."]
 S03["S03 — Implement delay-and-sum beamforming; evaluate MVDR with numerical conditioning constraints."]
 S04["S04 — Evaluate multiple-source association, covariance, split/merge and persistent acoustic-source re-ID."]
 S05["S05 — Compare configurations on annotated stationary/moving sources, reverberation and overlap."]
 M3G{{"M3 gate — Reproducible angular error, detection/track continuity and separation metrics against ODAS baseline."}}
 S01 --> M3G
 S02 --> M3G
 S03 --> M3G
 S04 --> M3G
 S05 --> M3G
end
subgraph M4["M4 — Speech and speaker processing / P1"]
direction TB
 V01["V01 — Evaluate far-field AEC3, denoising, resampling and speech-path level control using playback references."]
 V02["V02 — Validate VAD segmentation, pre/post-roll and offline/streaming ASR under low-SNR and overlap."]
 V03["V03 — Validate token/word timestamps, language identification, KWS and source-bound partial/final results."]
 V04["V04 — Evaluate speaker embeddings, verification, diarization and enrolled/unknown speaker decisions."]
 V05["V05 — Document TTS interruption, playback echo interactions and model-specific capability restrictions."]
 M4G{{"M4 gate — Measured WER, DER, keyword false alarms, AEC ERLE and source association on held-out recordings."}}
 V01 --> M4G
 V02 --> M4G
 V03 --> M4G
 V04 --> M4G
 V05 --> M4G
end
subgraph M5["M5 — Semantic events and acoustic memory / P1"]
direction TB
 I01["I01 — Benchmark YAMNet, EfficientAT, CLAP and sherpa with matched audio windows and label mappings."]
 I02["I02 — Separate raw scores from calibrated probabilities and validate unknown/open-vocabulary rejection."]
 I03["I03 — Extend temporal events, DTW, recurrence and motifs to recurring source-local acoustic patterns."]
 I04["I04 — Develop versioned sound prototypes, novelty detection and controlled long-term source memory."]
 I05["I05 — Expose source/event observations with provenance, uncertainty and explicit event lifecycles."]
 M5G{{"M5 gate — Held-out event F1/mAP, false-alarm rate, novelty and source-memory contamination measured."}}
 I01 --> M5G
 I02 --> M5G
 I03 --> M5G
 I04 --> M5G
 I05 --> M5G
end
subgraph M6["M6 — Developer SDK and deployment / P1–P2"]
direction TB
 E01["E01 — Provide concise recipes for classification, source tracking, speech transcription and replay."]
 E02["E02 — Maintain stable low-level C++ contracts; evaluate optional C and Python bindings."]
 E03["E03 — Profile queue bounds, allocation/lock behavior, p50/p95/p99 latency and multi-source load."]
 E04["E04 — Benchmark optimized CPU paths (AVX/NEON) and optional inference providers (OpenVINO/RKNN/CUDA)."]
 E05["E05 — Publish install guides, samples, feature maturity matrix and reproducible package profiles."]
 M6G{{"M6 gate — Independent consumer build and documented measured runtime/memory profile for supported targets."}}
 E01 --> M6G
 E02 --> M6G
 E03 --> M6G
 E04 --> M6G
 E05 --> M6G
end
subgraph ADD["M1–M6 proposed incremental extensions"]
direction TB
 D06["D06 (M1) — Expose cross-spectral covariance and inter-channel phase/level differences (IPD/ILD) with explicit geometry and spectral scaling"]
 T06["T06 (M2) — Trace source-generation epochs, sample offsets, source leakage and ASR attribution across gaps, reset and reuse"]
 S06["S06 (M3) — Evaluate localizer/tracker/separator interface split and source-specific baselines before adding advanced methods"]
 V06["V06 (M4) — Benchmark overlapped speech and target-speaker extraction, preserving verbatim ASR and attribution evidence"]
 I06["I06 (M5) — Standardize acoustic source/event evidence and bounded prototype references before constructing scenes or memory"]
 E06["E06 (M6) — Publish typed backend/model capability manifests and opt-in profiles for future processing-graph consumers"]
end
subgraph M7["M7 — Advanced spatial perception / P1 (proposed)"]
direction TB
 SP01["SP-01 — Introduce separately pluggable acoustic localizer, tracker, separator and spatial-mask contracts; keep ISpatialAudioEngine compatible"]
 SP02["SP-02 — Measure array impulse/transfer functions and version direct-path versus room-dependent calibration with geometry and uncertainty"]
 SP03["SP-03 — Benchmark noise-aware GEVD-MUSIC, multi-peak source counts and covariance regularization"]
 SP04["SP-04 — Evaluate robust MVDR/LCMV and GHDSS-style separation with residual-leakage postfilters against ODAS"]
 SP05["SP-05 — Evaluate optional WPE dereverberation and causal neural spatial masks without mandatory ML dependencies"]
 SP06["SP-06 — Offer direction-conditioned and enrolled-speaker target extraction with ambiguity and reject outputs"]
 SP07["SP-07 — Research spatial-energy maps and sound-event localization/detection subject to array-resolution limits"]
 SP08["SP-08 — Define an acoustic ego-noise model; robot motor and state inputs remain in consuming adapters"]
 M7G{{"M7 gate — Reference matched-array, matched-input DOA, SI-SDRi, leakage, source-specific WER, CPU and latency against ODAS and lawful HARK baseline."}}
 SP01 --> M7G
 SP02 --> M7G
 SP03 --> M7G
 SP04 --> M7G
 SP05 --> M7G
 SP06 --> M7G
 SP07 --> M7G
 SP08 --> M7G
end
subgraph M8["M8 — Acoustic scene model / P1 (proposed)"]
direction TB
 SC01["SC-01 — Define bounded, versioned AcousticSceneSnapshot plus timestamped source/event deltas and epoch-safe updates"]
 SC02["SC-02 — Represent Source–Event–Speaker–Observation relationships without conflating distinct identity types"]
 SC03["SC-03 — Describe acoustic-only spatial, temporal and co-occurrence relations; avoid unsupported causal conclusions"]
 SC04["SC-04 — Retain alternative hypotheses, absence of evidence, uncertainty and conservative association rules"]
 SC05["SC-05 — Attach timestamps, model/config provenance, quality and validity spans to every observation"]
 SC06["SC-06 — Detect acoustic scene arrivals, endings, reappearance candidates, ambience changes and stable background"]
 SC07["SC-07 — Aggregate overlapping source-specific events into bounded replayable episodes"]
 M8G{{"M8 gate — Independent scene-ID, link precision/recall, event-lifecycle and evidence-consistency tests across overlaps, reset, silence and track handoff."}}
 SC01 --> M8G
 SC02 --> M8G
 SC03 --> M8G
 SC04 --> M8G
 SC05 --> M8G
 SC06 --> M8G
 SC07 --> M8G
end
subgraph M9["M9 — Auditory attention and adaptive processing / P1–P2 (proposed)"]
direction TB
 AT01["AT-01 — Estimate acoustic salience from onset, novelty, persistence, urgency cues and evidence reliability"]
 AT02["AT-02 — Keep measured salience separate from external task relevance supplied by the application"]
 AT03["AT-03 — Model habituation, change-triggered re-alert and bounded long-running activity state"]
 AT04["AT-04 — Expose suggested source analysis depth, deadlines and cost without silently changing models"]
 AT05["AT-05 — Document opt-in eco, balanced and quality reference configurations and explicit fallback"]
 AT06["AT-06 — Offer caller-owned scheduling recommendations for graceful overload degradation"]
 AT07["AT-07 — Measure CPU/energy/latency benefits against recall, misses, WER, starvation and regressions"]
 M9G{{"M9 gate — Measured resource savings without unacceptable loss of event recall or speech quality; no robot/actuator decisions inside core."}}
 AT01 --> M9G
 AT02 --> M9G
 AT03 --> M9G
 AT04 --> M9G
 AT05 --> M9G
 AT06 --> M9G
 AT07 --> M9G
end
subgraph M10["M10 — Acoustic memory and semantic intelligence / P1–P2 (proposed)"]
direction TB
 AM01["AM-01 — Provide bounded optional short-term acoustic context and pre-roll without requiring stored PCM"]
 AM02["AM-02 — Define versioned episodic source/event indexes with application-owned persistence and retention"]
 AM03["AM-03 — Support controlled few-shot familiar-sound enrollment, prototype updates, rejection and contamination prevention"]
 AM04["AM-04 — Evaluate context-aware anomaly detection under domain shift with false alarms per hour"]
 AM05["AM-05 — Add audio-to-audio and text-to-audio retrieval through versioned embeddings"]
 AM06["AM-06 — Evaluate optional acoustic captions with grounded claims, uncertainty and licensing metadata"]
 AM07["AM-07 — Research evidence-bound audio question-answering that may abstain when audio evidence is insufficient"]
 AM08["AM-08 — Support explicit forgetting, expiry, deletion and redaction requests through consumer-facing contracts"]
 AM09["AM-09 — Define an optional versioned three-stream source descriptor: spatial position/motion/uncertainty, acoustic timbre/spectrum, and temporal change/motifs; preserve per-stream clocks, quality and provenance"]
 AM10["AM-10 — Implement modality-aware comparison separating physical-source re-identification from sound-type similarity, with missing-modality handling and spatial frame/motion assumptions"]
 AM11["AM-11 — Evaluate open-set nearest prototypes, novelty and optional offline clustering for unlabeled sounds; bound memory and reject low-confidence or contaminated observations"]
 AM12["AM-12 — Represent multi-window temporal embedding trajectories for repeated motifs, state changes and drift; preserve gaps and source-generation epochs"]
 AM13["AM-13 — Benchmark recognition across robot motion, reverberation, overlap and source leakage; measure retrieval, false merges/splits, unknown rejection, novelty alarms and resource cost"]
 M10G{{"M10 gate — Held-out retrieval, novelty and anomaly metrics; verified identity isolation, opt-in storage, bounded retention and model provenance."}}
 AM01 --> M10G
 AM02 --> M10G
 AM03 --> M10G
 AM04 --> M10G
 AM05 --> M10G
 AM06 --> M10G
 AM07 --> M10G
 AM08 --> M10G
 AM09 --> M10G
 AM10 --> M10G
 AM11 --> M10G
 AM12 --> M10G
 AM13 --> M10G
end
subgraph M11["M11 — Pipeline SDK and observability / P2 (proposed)"]
direction TB
 SDK01["SDK-01 — Design typed processing DAG nodes and port compatibility, lifecycle and memory-budget contracts"]
 SDK02["SDK-02 — Serialize validated YAML/JSON configurations as adapters over authoritative typed C++ configs"]
 SDK03["SDK-03 — Version graph schemas, migrations, dry-run diagnostics and configuration failures"]
 SDK04["SDK-04 — Supply documented opt-in speech, robotics and acoustic-monitoring recipes with measured costs"]
 SDK05["SDK-05 — Inspect graph processing: stage timings, audio leakage, source epochs, backpressure and errors"]
 SDK06["SDK-06 — Replay deterministic multichannel sessions with configuration, model hashes, discontinuities and observations"]
 SDK07["SDK-07 — Explore an optional visual graph editor outside core using the same graph validation API"]
 SDK08["SDK-08 — Expose explicit capability and model manifests and auditable backend selection"]
 SDK09["SDK-09 — Research a separately versioned plugin ABI only after proven demand; do not promise an ABI today"]
 M11G{{"M11 gate — Independent installed non-Qt C++17 consumer can validate a graph, reject mismatched ports, replay evidence and inspect bounded runtime telemetry."}}
 SDK01 --> M11G
 SDK02 --> M11G
 SDK03 --> M11G
 SDK04 --> M11G
 SDK05 --> M11G
 SDK06 --> M11G
 SDK07 --> M11G
 SDK08 --> M11G
 SDK09 --> M11G
end
subgraph QA["Cross-cutting verification / P0"]
direction TB
 Q01["Q01 — Maintain dataset/model manifests (SHA-256, licensing, geometry, sample rate, annotations)."]
 Q02["Q02 — Distinguish deterministic unit tests, model smoke, annotated benchmarks and physical HIL."]
 Q03["Q03 — Record representative multi-channel scenes: silence, faint/brief speech, moving sources, 1/2/5/10 sources."]
 Q04["Q04 — Measure task metrics: DOA error, ID switches, WER, DER, classification F1/mAP and AEC ERLE."]
 Q05["Q05 — Record runtime p50/p95/p99, throughput, RAM, alloc count, drops and power by platform."]
 Q06["Q06 — Establish baseline-relative regression limits; skipped external corpora are not passes."]
 Q07["Q07 — Compare spatial methods on identical raw multichannel audio, geometry, downstream ASR and scored SI-SDRi/SI-SIR/leakage/DOA/WER"]
 Q08["Q08 — Benchmark class- and speaker-conditioned extraction, association and explicit unknown/rejection behavior"]
 Q09["Q09 — Score uncertainty calibration and abstentions under room, array, direction and domain changes"]
 Q10["Q10 — Test scene association, alternative hypotheses, event relations, source generations and identity contamination"]
 Q11["Q11 — Stress concurrent sources and models, worker failures, restarts, memory bounds and p50/p95/p99 end-to-end latency"]
 Q12["Q12 — Compare adaptive processing CPU/RAM/energy savings against miss rate, event recall, WER and starvation"]
 Q13["Q13 — Verify corpus and model licensing, provenance, storage opt-in, forgetting, deletion and consumer privacy boundaries"]
 Q14["Q14 — Score three-stream unknown-sound discovery: same-source vs same-type retrieval, cluster purity, false merges/splits, open-set rejection, novelty false alarms/hour, domain shift and resource cost"]
 Q01 --> Q02 --> Q03 --> Q04 --> Q05 --> Q06 --> Q07 --> Q08 --> Q09 --> Q10 --> Q11 --> Q12 --> Q13 --> Q14
end
subgraph EXP["Optional research / not release blockers"]
direction TB
 X01["X01 — Passive acoustic mapping and room-signature estimation."]
 X02["X02 — Multiple synchronized microphone arrays and spatial consistency."]
 X03["X03 — Predictive event sequences and machine-condition anomaly detection."]
 X04["X04 — Continual/few-shot acoustic adaptation with explicit forgetting and drift evaluation."]
 X05["X05 — Auditory attention, spoofing/replay resistance and privacy controls."]
end
ROOT --> R01
ROOT --> Q01
M0G --> D01
M0G --> T01
M0G --> E01
M1G --> S01
M2G --> S01
M2G --> V01
M2G --> I01
M3G --> I04
M4G --> I05
M5G --> E02
M1G --> E03
Q06 -. verification .-> M2G
Q06 -. verification .-> M3G
Q06 -. verification .-> M4G
Q06 -. verification .-> M5G
Q06 -. verification .-> M6G
Q13 -. verification .-> M7G
Q13 -. verification .-> M8G
Q13 -. verification .-> M9G
Q13 -. verification .-> M10G
Q14 -. verification .-> M10G
Q13 -. verification .-> M11G
D06 --> M1G
T06 --> M2G
S06 --> M3G
V06 --> M4G
I06 --> M5G
E06 --> M6G
M1G --> SP01
M2G --> SP01
M3G --> SP02
M3G --> SC01
M5G --> SC01
M8G --> AT01
M8G --> AM01
M3G --> AM09
M5G --> AM09
M6G --> SDK01
M7G --> SDK04
M9G --> SDK04
M10G --> SDK04
M3G -. research .-> X01
M5G -. research .-> X03
BOUNDARY["Responsibility boundary<br/>libaudition: audio-only inference and observations<br/>Applications: ROS 2, TF, cameras, radar, PDDL and action policy"]
M6G --> BOUNDARY
classDef headline fill:#e9f0f7,stroke:#5e7c99,color:#1a324a,stroke-width:1.5px
classDef gate fill:#edf1f3,stroke:#60717a,color:#23303a,stroke-width:1.2px
classDef quality fill:#f4f0e7,stroke:#9b8b69,color:#403b2d
classDef research fill:#f2f2f2,stroke:#9a9a9a,color:#4c4c4c
class ROOT,BOUNDARY headline
class M0G,M1G,M2G,M3G,M4G,M5G,M6G,M7G,M8G,M9G,M10G,M11G gate
class Q01,Q02,Q03,Q04,Q05,Q06,Q07,Q08,Q09,Q10,Q11,Q12,Q13 quality
class X01,X02,X03,X04,X05 research
```

## Milestones and acceptance criteria

Stages identify **logical prerequisites**, not mandatory sequential implementation. In particular, quality infrastructure, DSP extensions and source-lifecycle work can proceed in parallel. Version references are planning labels.

### M0. Release baseline (P0)

*Baseline:* Release preparation. *Version grouping:* v1.0 RC; not a committed release target.

- **R01:** Verify the exact release commit against the full CI matrix (Linux, macOS, sanitizers, optional backends).
- **R02:** Freeze and document public C++17 contracts: AudioView, timestamps, clock domains, source and speaker IDs, uncertainty.
- **R03:** Validate CMake install/export, static/shared consumers, third-party notices and model provenance.
- **R04:** Document unsupported deployment claims and decide whether the release candidate can be tagged.

**Acceptance gate M0:** Exact-commit CI and independent installed consumer pass; exclusions and licenses recorded.

### M1. Reusable signal processing (P0)

*Baseline:* Planned expansion. *Version grouping:* Post-v1.0; not a committed release target.

- **D01:** Provide FFT/IFFT plans, real/complex transforms, frequency-bin conventions and documented scaling.
- **D02:** Add Hann/Hamming/Blackman windows and streaming STFT/ISTFT with defined reconstruction and latency.
- **D03:** Provide FIR/IIR/biquad filters, high-order fractional delay and explicit reset/flush behavior.
- **D04:** Expose spectral features: PSD, log-mel, MFCC, spectral flux, onset, pitch and harmonicity.
- **D05:** Add calibrated A/C weighting, noise-floor estimation and separately configurable AGC, gate and limiter.

**Acceptance gate M1:** Independent numerical references, reconstruction tolerances, chunk/reset tests and memory bounds.

### M2. Streaming and source lifecycle (P0)

*Baseline:* Existing components require improvement. *Version grouping:* Post-v1.0; not a committed release target.

- **T01:** Instrument per-channel levels, clipping, dropout, timestamp drift and capture-to-inference delays.
- **T02:** Introduce adaptive noise-floor estimation, onset thresholds, hysteresis, debounce and expiry tuning.
- **T03:** Specify source states: tentative, confirmed, inactive, lost and retired; prevent stale spatial tracks.
- **T04:** Harden per-source PCM epochs, backpressure, source retirement, late ASR results and ID reuse.
- **T05:** Keep spatial PCM phase-consistent; apply AEC/noise reduction/AGC on an explicit speech branch.

**Acceptance gate M2:** Quiet/short-source corpus regressions; no stale tracks or source contamination; bounded queues.

### M3. Spatial algorithms and tracking (P1)

*Baseline:* ODAS/GTSAM integrated; native methods planned. *Version grouping:* Post-v1.0; not a committed release target.

- **S01:** Version microphone-array geometry and measured gain, phase and delay calibration profiles.
- **S02:** Implement pluggable GCC-PHAT, SRP-PHAT and MUSIC localization baselines alongside ODAS.
- **S03:** Implement delay-and-sum beamforming; evaluate MVDR with numerical conditioning constraints.
- **S04:** Evaluate multiple-source association, covariance, split/merge and persistent acoustic-source re-ID.
- **S05:** Compare configurations on annotated stationary/moving sources, reverberation and overlap.

**Acceptance gate M3:** Reproducible angular error, detection/track continuity and separation metrics against ODAS baseline.

### M4. Speech and speaker processing (P1)

*Baseline:* Model adapters exist; quality validation planned. *Version grouping:* Post-v1.0; not a committed release target.

- **V01:** Evaluate far-field AEC3, denoising, resampling and speech-path level control using playback references.
- **V02:** Validate VAD segmentation, pre/post-roll and offline/streaming ASR under low-SNR and overlap.
- **V03:** Validate token/word timestamps, language identification, KWS and source-bound partial/final results.
- **V04:** Evaluate speaker embeddings, verification, diarization and enrolled/unknown speaker decisions.
- **V05:** Document TTS interruption, playback echo interactions and model-specific capability restrictions.

**Acceptance gate M4:** Measured WER, DER, keyword false alarms, AEC ERLE and source association on held-out recordings.

### M5. Semantic events and acoustic memory (P1)

*Baseline:* Classifier/temporal components exist; memory expansion planned. *Version grouping:* Post-v1.0; not a committed release target.

- **I01:** Benchmark YAMNet, EfficientAT, CLAP and sherpa with matched audio windows and label mappings.
- **I02:** Separate raw scores from calibrated probabilities and validate unknown/open-vocabulary rejection.
- **I03:** Extend temporal events, DTW, recurrence and motifs to recurring source-local acoustic patterns.
- **I04:** Develop versioned sound prototypes, novelty detection and controlled long-term source memory.
- **I05:** Expose source/event observations with provenance, uncertainty and explicit event lifecycles.

**Acceptance gate M5:** Held-out event F1/mAP, false-alarm rate, novelty and source-memory contamination measured.

### M6. Developer SDK and deployment (P1–P2)

*Baseline:* Incremental adoption work. *Version grouping:* Post-v1.0; not a committed release target.

- **E01:** Provide concise recipes for classification, source tracking, speech transcription and replay.
- **E02:** Maintain stable low-level C++ contracts; evaluate optional C and Python bindings.
- **E03:** Profile queue bounds, allocation/lock behavior, p50/p95/p99 latency and multi-source load.
- **E04:** Benchmark optimized CPU paths (AVX/NEON) and optional inference providers (OpenVINO/RKNN/CUDA).
- **E05:** Publish install guides, samples, feature maturity matrix and reproducible package profiles.

**Acceptance gate M6:** Independent consumer build and documented measured runtime/memory profile for supported targets.

## Incremental extensions of existing milestones (post-v1.0)

- **D06 (M1):** Expose cross-spectral covariance and inter-channel phase/level differences (IPD/ILD) with explicit geometry and spectral scaling.
- **T06 (M2):** Trace source-generation epochs, sample offsets, source leakage and ASR attribution across gaps, reset and reuse.
- **S06 (M3):** Evaluate localizer/tracker/separator interface split and source-specific baselines before adding advanced methods.
- **V06 (M4):** Benchmark overlapped speech and target-speaker extraction, preserving verbatim ASR and attribution evidence.
- **I06 (M5):** Standardize acoustic source/event evidence and bounded prototype references before constructing scenes or memory.
- **E06 (M6):** Publish typed backend/model capability manifests and opt-in profiles for future processing-graph consumers.

These supplement the existing M1–M6 definitions and are not retroactive release blockers. M3 retains the existing ODAS integration and its planned basic alternatives; M7 addresses advanced spatial research and quality parity.

## Proposed advanced milestones M7–M11

The following are planned capabilities, **not implemented feature claims**. Work can overlap where prerequisites and measurements permit. A custom algorithm should be justified by portability, measurable performance, capability gaps or licensing, rather than treating all research ideas as mandatory reinventions.

### M7. Advanced spatial perception (P1)

- **SP-01:** Introduce separately pluggable acoustic localizer, tracker, separator and spatial-mask contracts; keep ISpatialAudioEngine compatible.
- **SP-02:** Measure array impulse/transfer functions and version direct-path versus room-dependent calibration with geometry and uncertainty.
- **SP-03:** Benchmark noise-aware GEVD-MUSIC, multi-peak source counts and covariance regularization.
- **SP-04:** Evaluate robust MVDR/LCMV and GHDSS-style separation with residual-leakage postfilters against ODAS.
- **SP-05:** Evaluate optional WPE dereverberation and causal neural spatial masks without mandatory ML dependencies.
- **SP-06:** Offer direction-conditioned and enrolled-speaker target extraction with ambiguity and reject outputs.
- **SP-07:** Research spatial-energy maps and sound-event localization/detection subject to array-resolution limits.
- **SP-08:** Define an acoustic ego-noise model; robot motor and state inputs remain in consuming adapters.

**M7 acceptance gate:** Reference matched-array, matched-input DOA, SI-SDRi, leakage, source-specific WER, CPU and latency against ODAS and lawful HARK baseline.

### M8. Acoustic scene model (P1)

- **SC-01:** Define bounded, versioned AcousticSceneSnapshot plus timestamped source/event deltas and epoch-safe updates.
- **SC-02:** Represent Source–Event–Speaker–Observation relationships without conflating distinct identity types.
- **SC-03:** Describe acoustic-only spatial, temporal and co-occurrence relations; avoid unsupported causal conclusions.
- **SC-04:** Retain alternative hypotheses, absence of evidence, uncertainty and conservative association rules.
- **SC-05:** Attach timestamps, model/config provenance, quality and validity spans to every observation.
- **SC-06:** Detect acoustic scene arrivals, endings, reappearance candidates, ambience changes and stable background.
- **SC-07:** Aggregate overlapping source-specific events into bounded replayable episodes.

**M8 acceptance gate:** Independent scene-ID, link precision/recall, event-lifecycle and evidence-consistency tests across overlaps, reset, silence and track handoff.

### M9. Auditory attention and adaptive processing (P1–P2)

- **AT-01:** Estimate acoustic salience from onset, novelty, persistence, urgency cues and evidence reliability.
- **AT-02:** Keep measured salience separate from external task relevance supplied by the application.
- **AT-03:** Model habituation, change-triggered re-alert and bounded long-running activity state.
- **AT-04:** Expose suggested source analysis depth, deadlines and cost without silently changing models.
- **AT-05:** Document opt-in eco, balanced and quality reference configurations and explicit fallback.
- **AT-06:** Offer caller-owned scheduling recommendations for graceful overload degradation.
- **AT-07:** Measure CPU/energy/latency benefits against recall, misses, WER, starvation and regressions.

**M9 acceptance gate:** Measured resource savings without unacceptable loss of event recall or speech quality; no robot/actuator decisions inside core.

### M10. Acoustic memory and semantic intelligence (P1–P2)

- **AM-01:** Provide bounded optional short-term acoustic context and pre-roll without requiring stored PCM.
- **AM-02:** Define versioned episodic source/event indexes with application-owned persistence and retention.
- **AM-03:** Support controlled few-shot familiar-sound enrollment, prototype updates, rejection and contamination prevention.
- **AM-04:** Evaluate context-aware anomaly detection under domain shift with false alarms per hour.
- **AM-05:** Add audio-to-audio and text-to-audio retrieval through versioned embeddings.
- **AM-06:** Evaluate optional acoustic captions with grounded claims, uncertainty and licensing metadata.
- **AM-07:** Research evidence-bound audio question-answering that may abstain when audio evidence is insufficient.
- **AM-08:** Support explicit forgetting, expiry, deletion and redaction requests through consumer-facing contracts.
- **AM-09:** Define an optional, versioned three-stream `SourceRepresentation`: **spatial** (direction/position/trajectory with coordinate-frame and motion assumptions), **acoustic** (spectrum/timbre/optional learned embedding) and **temporal** (multi-scale feature trajectories and motifs). Each stream carries its own validity interval, source-generation ID, dimensionality/model/config version, quality, uncertainty and provenance.
- **AM-10:** Compare physical-source identity and sound-type similarity **separately**. Use context-dependent modality gating; spatial disagreement must not prohibit recognition of the same *kind* of sound elsewhere, while sound similarity alone must never assert that two observations are the same physical source.
- **AM-11:** Evaluate unknown-sound similarity, versioned prototypes, novelty detection and optional offline clustering (e.g. HDBSCAN), with explicit unknown/abstain outputs, delayed corroboration, bounded storage and safeguards against separation leakage and mixed-source contamination.
- **AM-12:** Encode temporal evolution with short and longer windows, including transients, periodic patterns, temporal alignment and drift/change detection. Preserve silence, discontinuities, late observations and source-generation epochs; do not silently merge unrelated tracks.
- **AM-13:** Evaluate on reproducible multichannel simulation and held-out recordings: stationary/moving sources and observer, reverberation, overlapping speakers, machinery/noise and intermittent external events. Report same-source vs same-type retrieval, cluster purity, false merges/splits, open-set rejection, novelty false alarms/hour, confidence calibration and CPU/RAM/p95 latency against acoustic-only and flat-embedding baselines.

**M10 acceptance gate:** Held-out retrieval, novelty and anomaly metrics; verified identity isolation, opt-in storage, bounded retention and model provenance. For AM-09–AM-13 additionally require separately scored physical-source identity versus sound-type matching, reliable unknown/rejection behavior, contamination tests, and baseline-relative performance/resource measurements.

**Deferred research slice — three-stream source representation and acoustic memory.** This is a future capability, not a current API or an instruction to modify working classifiers/separation. Start with deterministic DSP/spatial/temporal features and an optional feature backend; compare against flat acoustic embeddings before introducing learned models. Spatial representation must declare its coordinate frame, and ego-motion compensation requires explicitly supplied data from an application adapter (no ROS 2 or robot-policy dependency in core). Similarity scores are evidence, not proof of common physical origin or causal relations. Evaluate first with deterministic synthetic fixtures, then a separate acoustics-simulation corpus (for example, a 7×7 m room with seven overlapping speakers, a moving listener, appliance noise, and intermittent exterior traffic), and finally held-out annotated recordings. Do not mark the stage complete merely because CI passes: measure accuracy, leakage robustness, false alarms, memory, and latency on reproducible datasets.

### M11. Pipeline SDK and observability (P2)

- **SDK-01:** Design typed processing DAG nodes and port compatibility, lifecycle and memory-budget contracts.
- **SDK-02:** Serialize validated YAML/JSON configurations as adapters over authoritative typed C++ configs.
- **SDK-03:** Version graph schemas, migrations, dry-run diagnostics and configuration failures.
- **SDK-04:** Supply documented opt-in speech, robotics and acoustic-monitoring recipes with measured costs.
- **SDK-05:** Inspect graph processing: stage timings, audio leakage, source epochs, backpressure and errors.
- **SDK-06:** Replay deterministic multichannel sessions with configuration, model hashes, discontinuities and observations.
- **SDK-07:** Explore an optional visual graph editor outside core using the same graph validation API.
- **SDK-08:** Expose explicit capability and model manifests and auditable backend selection.
- **SDK-09:** Research a separately versioned plugin ABI only after proven demand; do not promise an ABI today.

**M11 acceptance gate:** Independent installed non-Qt C++17 consumer can validate a graph, reject mismatched ports, replay evidence and inspect bounded runtime telemetry.

## Cross-cutting verification (P0)

- **Q01:** Maintain dataset/model manifests (SHA-256, licensing, geometry, sample rate, annotations).
- **Q02:** Distinguish deterministic unit tests, model smoke, annotated benchmarks and physical HIL.
- **Q03:** Record representative multi-channel scenes: silence, faint/brief speech, moving sources, 1/2/5/10 sources.
- **Q04:** Measure task metrics: DOA error, ID switches, WER, DER, classification F1/mAP and AEC ERLE.
- **Q05:** Record runtime p50/p95/p99, throughput, RAM, alloc count, drops and power by platform.
- **Q06:** Establish baseline-relative regression limits; skipped external corpora are not passes.
- **Q07:** Compare spatial methods on identical raw multichannel audio, geometry, downstream ASR and scored SI-SDRi/SI-SIR/leakage/DOA/WER.
- **Q08:** Benchmark class- and speaker-conditioned extraction, association and explicit unknown/rejection behavior.
- **Q09:** Score uncertainty calibration and abstentions under room, array, direction and domain changes.
- **Q10:** Test scene association, alternative hypotheses, event relations, source generations and identity contamination.
- **Q11:** Stress concurrent sources and models, worker failures, restarts, memory bounds and p50/p95/p99 end-to-end latency.
- **Q12:** Compare adaptive processing CPU/RAM/energy savings against miss rate, event recall, WER and starvation.
- **Q13:** Verify corpus and model licensing, provenance, storage opt-in, forgetting, deletion and consumer privacy boundaries.
- **Q14:** Benchmark three-stream embeddings and unknown-sound discovery with separate ground truth for physical-source identity and sound-type similarity; include spatial motion, room changes, overlap/leakage, confidence calibration, false merges/splits, cluster purity, novelty false alarms/hour and resource cost.

**Evidence requirements:** Each reported benchmark must identify library commit, dataset/annotation revision, model hash, array geometry, configuration, scoring definition and hardware. Numerical release thresholds must be based on recorded baselines, not invented in advance.

The maintained [algorithm test matrix](algorithm_test_matrix.md) describes which results are contract tests and which have independent evidence. External data not configured in CI must be recorded as **not evaluated**, not treated as passing.

## Optional research directions (not release blockers)

- **X01:** Passive acoustic mapping and room-signature estimation.
- **X02:** Multiple synchronized microphone arrays and spatial consistency.
- **X03:** Predictive event sequences and machine-condition anomaly detection.
- **X04:** Continual/few-shot acoustic adaptation with explicit forgetting and drift evaluation.
- **X05:** Auditory attention, spoofing/replay resistance and privacy controls.

Research results require feasibility studies and independently defined metrics before promotion into a release scope.

## Architecture boundary

The library ends at audio-derived signals, source tracking, classification, speech/speaker information, temporal patterns, uncertainty and acoustic events. The consuming application remains responsible for ROS 2 integration, localization/TF, camera or radar fusion, navigation, task planning, recording consent and retention policies.

## Maintenance rules

1. Keep shipped and proposed functionality visibly distinct in documentation and demos.
2. Add an independent numerical reference or a model-backed reference fixture where appropriate, plus state/lifecycle/invalid-input tests.
3. Maintain source-local identity and clock semantics, bounded resource consumption and error recovery.
4. Profile physical platforms separately; do not transfer x86 accuracy or timing claims to RK3588 without measurement.
5. Update this roadmap only when acceptance evidence, dependencies or engineering priorities change.
6. M7–M11 must not delay v1.0 stability, source lifecycle fixes or Qt speech reliability.
7. Compare advanced algorithms on matched recordings before claiming superiority over ODAS/HARK; record microphone geometry and array count.
8. Preserve ambiguity, abstentions and evidence provenance; acoustic co-occurrence does not by itself imply causality or a human identity.
9. Scene, memory, attention and DAG capabilities must remain modular, resource-bounded and application-independent.
